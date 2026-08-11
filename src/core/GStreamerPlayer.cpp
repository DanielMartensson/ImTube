#include "core/GStreamerPlayer.h"
#include "core/PlaybackRate.h"
#include "core/YtDlpHelper.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video-info.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace imtube {

namespace {

// Debug tracing enabled with IMTUBE_DEBUG=1.
inline bool dbg_enabled()
{
    static const bool on = getenv("IMTUBE_DEBUG") != nullptr;
    return on;
}
#define DBG(fmt, ...)                                                        \
    do                                                                       \
    {                                                                        \
        if (dbg_enabled())                                                   \
            fprintf(stderr, "[player] " fmt "\n", ##__VA_ARGS__);            \
    } while (0)

void terminate_child(pid_t pid)
{
    if (pid <= 0)
        return;
    kill(pid, SIGTERM);
    for (int i = 0; i < 40; i++) // ~2s grace period
    {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(50 * 1000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

} // namespace

bool GStreamerPlayer::ensure_gst_initialized()
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        GError* err = nullptr;
        ok = gst_init_check(nullptr, nullptr, &err);
        if (!ok)
        {
            fprintf(stderr, "[gstreamer] gst_init_check() failed: %s\n",
                    err && err->message ? err->message : "unknown error");
            if (err)
                g_error_free(err);
        }
    });
    return ok;
}

bool GStreamerPlayer::start(const std::string& url, bool live, int max_height, const std::string& ytdlp_binary)
{
    if (!ensure_gst_initialized())
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "GStreamer could not be initialized on this system";
        return false;
    }

    if (m_started)
        stop();

    m_live = live;

    // --- Launch yt-dlp (media stream on stdout) ------------------------------
    YtDlpHelper helper(ytdlp_binary);
    const int fd = helper.launch_stream(url, live, max_height, &m_child_pid);
    if (fd < 0)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "Failed to launch yt-dlp (is it installed?)";
        return false;
    }
    m_pipe_fd = fd;
    m_ytdlp_log = helper.last_stderr_log();
    m_saw_frame = false;

    // --- Build the GStreamer pipeline ----------------------------------------
    m_pipeline = gst_pipeline_new("imtube-pipeline");
    m_appsrc = gst_element_factory_make("appsrc", "src");
    GstElement* decodebin = gst_element_factory_make("decodebin", "dec");
    GstElement* appsink_elem = gst_element_factory_make("appsink", "vsink");
    m_appsink = GST_APP_SINK(appsink_elem);

    if (!m_pipeline || !m_appsrc || !decodebin || !appsink_elem)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "GStreamer could not create the playback pipeline";
        teardown();
        terminate_child(m_child_pid);
        m_child_pid = -1;
        if (m_pipe_fd >= 0) { close(m_pipe_fd); m_pipe_fd = -1; }
        return false;
    }

    // appsrc: accepts an unknown container from yt-dlp's stdout; decodebin
    // typefinds and demuxes it. Timestamps are derived from the pipeline clock.
    gst_app_src_set_stream_type(GST_APP_SRC(m_appsrc), GST_APP_STREAM_TYPE_STREAM);
    g_object_set(m_appsrc, "do-timestamp", TRUE, nullptr);

    // appsink: keep only the newest frame and drop stale ones for low latency.
    gst_app_sink_set_max_buffers(m_appsink, 1);
    gst_app_sink_set_drop(m_appsink, TRUE);
    gst_app_sink_set_wait_on_eos(m_appsink, FALSE);
    GstCaps* vcaps = gst_caps_from_string("video/x-raw,format=RGBA");
    gst_app_sink_set_caps(m_appsink, vcaps);
    gst_caps_unref(vcaps);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, decodebin, appsink_elem, nullptr);
    if (!gst_element_link(m_appsrc, decodebin))
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "Failed to link the GStreamer pipeline";
        teardown();
        terminate_child(m_child_pid);
        m_child_pid = -1;
        if (m_pipe_fd >= 0) { close(m_pipe_fd); m_pipe_fd = -1; }
        return false;
    }

    g_signal_connect(decodebin, "pad-added", G_CALLBACK(+[](GstElement* elem, GstPad* pad, gpointer user_data) {
        static_cast<GStreamerPlayer*>(user_data)->on_pad_added(elem, pad);
    }), this);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    m_error.clear();
    m_eos = false;
    m_paused = false;
    m_playing = true;
    m_started = true;

    // Stream time starts at zero; keep it across rate changes in m_segment.base.
    gst_segment_init(&m_segment, GST_FORMAT_TIME);

    try
    {
        m_feeder_running = true;
        m_feeder = std::thread(&GStreamerPlayer::feeder_loop, this);
    }
    catch (const std::exception&)
    {
        m_feeder_running = false;
        stop();
        return false;
    }
    return true;
}

void GStreamerPlayer::stop()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_started)
        {
            // Belt and braces: release anything left over.
            teardown();
            return;
        }
        m_started = false;
    }

    m_feeder_running = false;

    // Stop producing data first, then flush the pipeline while the appsrc is
    // still alive so a feeder thread blocked in gst_app_src_push_buffer()
    // returns immediately. Only then join the feeder and release the pipeline,
    // otherwise the feeder can touch a freed appsrc.
    terminate_child(m_child_pid);
    m_child_pid = -1;

    if (m_pipe_fd >= 0)
    {
        close(m_pipe_fd);
        m_pipe_fd = -1;
    }

    if (m_pipeline != nullptr)
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

    if (m_feeder.joinable())
        m_feeder.join();

    teardown();

    m_playing = false;
    m_paused = false;
    m_eos = false;
}

void GStreamerPlayer::set_paused(bool paused)
{
    if (!m_pipeline)
        return;
    m_paused = paused;
    gst_element_set_state(m_pipeline, paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}

bool GStreamerPlayer::toggle_pause()
{
    set_paused(!m_paused);
    return m_paused;
}

bool GStreamerPlayer::get_position_and_duration(int64_t* pos_ms, int64_t* dur_ms) const
{
    if (m_pipeline == nullptr)
        return false;

    gint64 pos = 0;
    gint64 dur = 0;
    const bool pos_ok = gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos);
    const bool dur_ok = gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur);

    if (pos_ms)
        *pos_ms = pos_ok ? (int64_t)(pos / GST_MSECOND) : -1;
    if (dur_ms)
        *dur_ms = dur_ok ? (int64_t)(dur / GST_MSECOND) : -1;
    return pos_ok || dur_ok;
}

bool GStreamerPlayer::set_playback_speed(double rate)
{
    if (m_pipeline == nullptr)
        return false;

    // Query the position BEFORE locking m_mutex: link_branch() runs on the
    // streaming thread and takes the same lock, so we must not hold it while
    // asking the pipeline for state.
    gint64 pos = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos))
        pos = GST_CLOCK_TIME_NONE;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_branch_pads.empty())
        return false; // no decoded stream yet

    if (pos == GST_CLOCK_TIME_NONE)
        pos = (gint64)m_segment.position;

    // A SEGMENT event only re-times what already flows downstream of decodebin;
    // it never asks the demuxer to seek, which is exactly what we want because
    // the byte stream from yt-dlp's stdout cannot be repositioned.
    GstSegment seg;
    if (!rate::change_segment(m_segment, (guint64)pos, rate, seg))
        return false;

    GstEvent* event = gst_event_new_segment(&seg);
    for (GstPad* pad : m_branch_pads)
    {
        if (pad != nullptr && gst_pad_is_linked(pad))
            gst_pad_send_event(pad, gst_event_ref(event));
    }
    gst_event_unref(event);

    m_segment = seg;
    m_speed = rate;
    DBG("set_playback_speed(%.2f) pos=%" G_GINT64_FORMAT " base=%" G_GUINT64_FORMAT,
        rate, pos, seg.base);
    return true;
}

void GStreamerPlayer::set_volume(float volume)
{
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    m_volume = volume;
    if (m_volume_elem != nullptr)
        g_object_set(m_volume_elem, "volume", (double)(m_muted ? 0.0 : volume), nullptr);
}

void GStreamerPlayer::set_muted(bool muted)
{
    m_muted = muted;
    if (m_volume_elem != nullptr)
        g_object_set(m_volume_elem, "volume", (double)(muted ? 0.0 : m_volume), nullptr);
}

void GStreamerPlayer::on_pad_added(GstElement* /*decodebin*/, GstPad* pad)
{
    if (pad == nullptr || gst_pad_is_linked(pad))
        return;

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (caps == nullptr)
    {
        // Caps are not negotiated yet; wait for them via the notify::caps signal.
        g_signal_connect(pad, "notify::caps", G_CALLBACK(+[](GstPad* p, GParamSpec*, gpointer user_data) {
            static_cast<GStreamerPlayer*>(user_data)->on_pad_added(nullptr, p);
        }), this);
        return;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const char* media_type = gst_structure_get_name(structure);
    if (g_str_has_prefix(media_type, "video/"))
        link_branch(pad, true);
    else if (g_str_has_prefix(media_type, "audio/"))
        link_branch(pad, false);

    gst_caps_unref(caps);
}

void GStreamerPlayer::link_branch(GstPad* pad, bool is_video)
{
    GstElement* chain[5] = {};
    const char* names[5];

    if (is_video)
    {
        // decodebin -> queue -> videoconvert -> videoscale -> capsfilter(RGBA) -> appsink
        names[0] = "queue";
        names[1] = "videoconvert";
        names[2] = "videoscale";
        names[3] = "capsfilter";
    }
    else
    {
        // decodebin -> queue -> audioconvert -> volume -> audioresample -> autoaudiosink.
        // The audio decoder emits non-interleaved raw audio while volume (and
        // most of the chain) only handles interleaved, so audioconvert goes first
        // to convert the layout at runtime.
        names[0] = "queue";
        names[1] = "audioconvert";
        names[2] = "volume";
        names[3] = "audioresample";
        names[4] = "autoaudiosink";
    }

    const int chain_len = is_video ? 4 : 5;
    for (int i = 0; i < chain_len; i++)
    {
        chain[i] = gst_element_factory_make(names[i], nullptr);
        if (chain[i] == nullptr)
        {
            for (int j = 0; j < i; j++)
                gst_object_unref(chain[j]);
            fprintf(stderr, "[gstreamer] could not create element '%s'\n", names[i]);
            return;
        }
    }

    if (is_video)
    {
        GstCaps* caps = gst_caps_from_string("video/x-raw,format=RGBA");
        g_object_set(chain[3], "caps", caps, nullptr);
        gst_caps_unref(caps);
    }
    else
    {
        // Apply the current volume to the new branch (and remember the element so
        // later volume changes reach it too).
        m_volume_elem = chain[2];
        g_object_set(m_volume_elem, "volume", (double)(m_muted ? 0.0 : m_volume), nullptr);
    }
    DBG("link_branch(%s): creating branch", is_video ? "video" : "audio");

    gst_bin_add_many(GST_BIN(m_pipeline), chain[0], chain[1], chain[2], chain[3],
                     is_video ? nullptr : chain[4], nullptr);
    gst_element_link_many(chain[0], chain[1], chain[2], chain[3],
                          is_video ? nullptr : chain[4], nullptr);

    if (is_video)
        gst_element_link(chain[3], GST_ELEMENT_CAST(m_appsink));

    // Link the decodebin source pad to the start of the branch BEFORE syncing
    // state. A chain that is not fed from upstream would otherwise never
    // complete preroll (e.g. an orphaned autoaudiosink blocks the whole
    // pipeline). If the link fails, remove the orphaned elements again so the
    // rest of the pipeline can still start.
    GstPad* sinkpad = gst_element_get_static_pad(chain[0], "sink");
    bool linked = false;
    if (sinkpad != nullptr)
    {
        // Link without a caps check: the decodebin source pad can be exposed
        // before its caps land on it, and the downstream caps query would force
        // an interleaved layout that clashes with the decoder's non-interleaved
        // output (audioconvert converts the layout at runtime instead).
        GstPadLinkReturn ret =
            gst_pad_link_full(pad, sinkpad, (GstPadLinkCheck)GST_PAD_LINK_CHECK_NOTHING);
        DBG("link_branch(%s): pad link ret=%d", is_video ? "video" : "audio", (int)ret);
        linked = ret == GST_PAD_LINK_OK;
        gst_object_unref(sinkpad);
    }

    if (!linked)
    {
        fprintf(stderr, "[gstreamer] pad link failed for %s branch, dropping it\n",
                is_video ? "video" : "audio");
        for (int i = 0; i < chain_len; i++)
        {
            gst_element_set_state(chain[i], GST_STATE_NULL);
            gst_bin_remove(GST_BIN(m_pipeline), chain[i]);
        }
        if (!is_video)
            m_volume_elem = nullptr;
        return;
    }

    for (int i = 0; i < chain_len; i++)
        gst_element_sync_state_with_parent(chain[i]);

    // Remember the decodebin source pad (ref'd) so set_playback_speed() can
    // push a rate-changing SEGMENT event onto this branch.
    gst_object_ref(pad);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_branch_pads.push_back(pad);
    }
}

void GStreamerPlayer::feeder_loop()
{
    constexpr size_t kChunk = 128 * 1024;
    std::vector<char> buf(kChunk);
    std::vector<char> pending; // bytes read from the pipe but not yet pushed

    while (m_feeder_running)
    {
        if (pending.empty())
        {
            const ssize_t n = read(m_pipe_fd, buf.data(), buf.size());
            if (n > 0)
            {
                pending.assign(buf.data(), buf.data() + n);
            }
            else if (n == 0)
            {
                gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
                break;
            }
            else
            {
                if (errno == EINTR)
                    continue;
                if (m_feeder_running)
                    gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
                break;
            }
        }

        GstBuffer* gbuf = gst_buffer_new_allocate(nullptr, (gsize)pending.size(), nullptr);
        gst_buffer_fill(gbuf, 0, pending.data(), (gsize)pending.size());
        const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), gbuf);
        if (dbg_enabled() && (m_push_count % 32) == 0)
            DBG("feeder pushed %zu bytes (ret=%s)", pending.size(), gst_flow_get_name(ret));
        m_push_count++;
        if (ret == GST_FLOW_OK)
        {
            pending.clear();
        }
        else if (ret == GST_FLOW_FLUSHING)
        {
            // A flush (e.g. a playback-rate seek) is in progress. Keep the bytes
            // and retry; otherwise a rate change would kill the feed.
            g_usleep(2000);
        }
        else
        {
            gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
            break;
        }
    }
}

bool GStreamerPlayer::pull_frame()
{
    if (m_appsink == nullptr || !m_playing)
    {
        DBG("pull_frame: no appsink or not playing");
        return false;
    }

    GstSample* sample = gst_app_sink_try_pull_sample(m_appsink, 0);
    if (sample == nullptr)
        return false;

    bool got = false;
    GstCaps* caps = gst_sample_get_caps(sample);
    if (caps != nullptr)
    {
        GstVideoInfo info;
        if (gst_video_info_from_caps(&info, caps))
        {
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                DBG("pull_frame: got %dx%d frame", info.width, info.height);
                m_frame_w = info.width;
                m_frame_h = info.height;
                m_frame.assign(map.data, map.data + map.size);
                gst_buffer_unmap(buffer, &map);
                got = true;
                m_saw_frame = true;
            }
        }
    }
    gst_sample_unref(sample);
    return got;
}

void GStreamerPlayer::pump_bus()
{
    if (m_pipeline == nullptr)
        return;

    GstBus* bus = gst_element_get_bus(m_pipeline);
    if (bus == nullptr)
        return;

    while (true)
    {
        GstMessage* msg = gst_bus_pop_filtered(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (msg == nullptr)
            break;

        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
        {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            DBG("bus: ERROR '%s' (%s)", err ? err->message : "?", dbg ? dbg : "");
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_error = err && err->message ? err->message : "Unknown GStreamer error";
            }
            if (!m_saw_frame)
                append_ytdlp_error("yt-dlp output that may explain the failure:");
            if (err)
                g_error_free(err);
            if (dbg)
                g_free(dbg);
            stop();
        }
        else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS)
        {
            m_eos = true;
            if (!m_saw_frame)
                append_ytdlp_error(
                    "The stream ended before any video was decoded; yt-dlp likely "
                    "failed to fetch this video. Last yt-dlp output:");
            stop();
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
}

std::string GStreamerPlayer::error() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_error;
}

void GStreamerPlayer::append_ytdlp_error(const char* prefix)
{
    if (m_ytdlp_log.empty())
        return;

    FILE* f = fopen(m_ytdlp_log.c_str(), "rb");
    if (f == nullptr)
        return;

    const size_t kMax = 4000;
    std::vector<char> data(kMax + 1);
    const size_t n = fread(data.data(), 1, kMax, f);
    fclose(f);
    if (n == 0)
        return;

    // Keep only the tail so the meaningful error lines survive.
    size_t start = 0;
    if (n == kMax)
    {
        for (size_t i = n; i > 0; i--)
        {
            if (data[i - 1] == '\n')
            {
                start = i;
                break;
            }
        }
    }
    data[n] = '\0';

    std::lock_guard<std::mutex> lk(m_mutex);
    std::string& err = m_error;
    if (err.empty())
        err = prefix;
    else
        err += "\n";
    err += "\n";
    err += prefix;
    err += "\n";
    err += std::string(data.data() + start);
}

void GStreamerPlayer::teardown()
{
    if (m_pipeline != nullptr)
    {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc = nullptr;
        m_appsink = nullptr;
        m_volume_elem = nullptr;
    }

    for (GstPad* pad : m_branch_pads)
    {
        if (pad != nullptr)
            gst_object_unref(pad);
    }
    m_branch_pads.clear();

    m_frame.clear();
    m_frame_w = 0;
    m_frame_h = 0;
}

} // namespace imtube
