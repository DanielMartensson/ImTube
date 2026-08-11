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

/* Debug tracing enabled with IMTUBE_DEBUG=1. */
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

/* Terminate a child process, with a short SIGTERM grace period before SIGKILL. */
void terminate_child(pid_t pid)
{
    if (pid <= 0)
        return;
    kill(pid, SIGTERM);
    for (int i = 0; i < 40; i++) /* ~2s grace period */
    {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(50 * 1000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

} /* namespace */

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
    m_url = url;
    m_max_height = max_height;
    m_ytdlp_binary = ytdlp_binary;
    m_start_offset_ms = 0;

    /* --- Launch yt-dlp (media stream on stdout) ------------------------------ */
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

    if (!start_pipeline())
    {
        teardown();
        terminate_child(m_child_pid);
        m_child_pid = -1;
        if (m_pipe_fd >= 0) { close(m_pipe_fd); m_pipe_fd = -1; }
        return false;
    }

    /* Warm the direct-URL cache in the background so a later seek does not
     * have to wait for "yt-dlp -g". */
    start_direct_url_fetch();
    return true;
}

bool GStreamerPlayer::start_pipeline()
{
    /* --- Build the GStreamer pipeline ---------------------------------------- */
    m_pipeline = gst_pipeline_new("imtube-pipeline");
    m_appsrc = gst_element_factory_make("appsrc", "src");
    GstElement* decodebin = gst_element_factory_make("decodebin", "dec");
    GstElement* appsink_elem = gst_element_factory_make("appsink", "vsink");
    m_appsink = GST_APP_SINK(appsink_elem);

    if (!m_pipeline || !m_appsrc || !decodebin || !appsink_elem)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "GStreamer could not create the playback pipeline";
        return false;
    }

    /* appsrc: accepts an unknown container from the byte source (yt-dlp's
     * stdout or ffmpeg's matroska segment); decodebin typefinds and demuxes it.
     * The raw bytes carry no timestamps of their own: the demuxer (matroskademux)
     * derives the media timestamps from the container, so do-timestamp stays OFF.
     * With do-timestamp=TRUE appsrc would stamp the first buffer with the wall
     * clock time the moment data finally arrives (after yt-dlp's download/merge
     * delay), which makes the reported position start several seconds in. */
    gst_app_src_set_stream_type(GST_APP_SRC(m_appsrc), GST_APP_STREAM_TYPE_STREAM);
    g_object_set(m_appsrc, "do-timestamp", FALSE, nullptr);

    /* appsink: keep only the newest frame, drop stale ones for low latency and
     * sync to the pipeline clock so playback rate changes (the SEGMENT events
     * pushed by set_playback_speed()) also speed up the video, not just audio. */
    gst_app_sink_set_max_buffers(m_appsink, 1);
    gst_app_sink_set_drop(m_appsink, TRUE);
    gst_app_sink_set_wait_on_eos(m_appsink, FALSE);
    g_object_set(m_appsink, "sync", TRUE, nullptr);
    GstCaps* vcaps = gst_caps_from_string("video/x-raw,format=RGBA");
    gst_app_sink_set_caps(m_appsink, vcaps);
    gst_caps_unref(vcaps);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, decodebin, appsink_elem, nullptr);
    if (!gst_element_link(m_appsrc, decodebin))
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_error = "Failed to link the GStreamer pipeline";
        return false;
    }

    /* Debug probe: log SEGMENT events reaching the appsink (their rate is what
     * the sink uses to pace presentation). */
    if (dbg_enabled())
    {
        GstPad* vsink_pad = gst_element_get_static_pad(appsink_elem, "sink");
        if (vsink_pad != nullptr)
        {
            gst_pad_add_probe(
                vsink_pad, (GstPadProbeType)GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                +[](GstPad*, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
                    GstEvent* ev = GST_EVENT_CAST(info->data);
                    if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT)
                    {
                        GstSegment seg;
                        const GstSegment* segp = nullptr;
                        gst_event_parse_segment(ev, &segp);
                        if (segp != nullptr)
                            seg = *segp;
                        DBG("appsink SEGMENT: rate=%.2f start=%" G_GUINT64_FORMAT
                            " stop=%" G_GUINT64_FORMAT " base=%" G_GUINT64_FORMAT,
                            seg.rate, seg.start, seg.stop, seg.base);
                    }
                    return GST_PAD_PROBE_OK;
                },
                nullptr, nullptr);
            gst_object_unref(vsink_pad);
        }
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

    /* Stream time starts at zero; keep it across rate changes in m_segment.base. */
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
            /* Belt and braces: release anything left over. */
            teardown();
            cancel_direct_url_fetch();
            return;
        }
        m_started = false;
    }

    cancel_direct_url_fetch();
    m_feeder_running = false;

    /* Stop producing data first, then flush the pipeline while the appsrc is
     * still alive so a feeder thread blocked in gst_app_src_push_buffer()
     * returns immediately. Only then join the feeder and release the pipeline,
     * otherwise the feeder can touch a freed appsrc. */
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

    /* After a seek the byte source is a section of the video re-based to zero;
     * m_start_offset_ms maps its stream time back to real media time. */
    if (pos_ms)
        *pos_ms = pos_ok ? (int64_t)(pos / GST_MSECOND) + m_start_offset_ms : -1;

    /* A streamed container (matroska from the ffmpeg merge) cannot report its
     * total length until EOS, so the demuxer's duration only ever shows the
     * last parsed cluster. Use the metadata duration when we know it, and fall
     * back to the demuxer's answer otherwise (live streams, raw URLs). */
    if (dur_ms)
    {
        if (m_known_duration_ms > 0)
            *dur_ms = m_known_duration_ms;
        else
            *dur_ms = dur_ok ? (int64_t)(dur / GST_MSECOND) + m_start_offset_ms : -1;
    }
    return pos_ok || dur_ok;
}

bool GStreamerPlayer::set_playback_speed(double rate)
{
    if (m_pipeline == nullptr)
        return false;

    /* Query the position BEFORE locking m_mutex: link_branch() runs on the
     * streaming thread and takes the same lock, so we must not hold it while
     * asking the pipeline for state. */
    gint64 pos = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos))
        pos = GST_CLOCK_TIME_NONE;

    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_branch_pads.empty())
        return false; /* no decoded stream yet */

    if (pos == GST_CLOCK_TIME_NONE)
        pos = (gint64)m_segment.position;

    /* A SEGMENT event only re-times what already flows downstream of decodebin;
     * it never asks the demuxer to seek, which is exactly what we want because
     * the byte stream from yt-dlp's stdout cannot be repositioned. */
    GstSegment seg;
    if (!rate::change_segment(m_segment, (guint64)pos, rate, seg))
        return false;

    GstEvent* event = gst_event_new_segment(&seg);
    for (GstPad* pad : m_branch_pads)
    {
        /* Send to the queue's sink pad (the peer of the decodebin src pad):
         * gst_pad_send_event() on the decodebin src pad itself returns FALSE
         * for these internal pads, but the queue's sink event function accepts
         * and forwards a mid-stream SEGMENT. */
        GstPad* peer = gst_pad_get_peer(pad);
        if (peer != nullptr)
        {
            const gboolean sent = gst_pad_send_event(peer, gst_event_ref(event));
            DBG("set_playback_speed: sent segment to %s (peer of %s) -> %s",
                GST_PAD_NAME(peer), GST_PAD_NAME(pad), sent ? "TRUE" : "FALSE");
            gst_object_unref(peer);
        }
        else
        {
            DBG("set_playback_speed: no peer for pad=%s", GST_PAD_NAME(pad));
        }
    }
    gst_event_unref(event);

    m_segment = seg;
    m_speed = rate;
    DBG("set_playback_speed(%.2f) pos=%" G_GINT64_FORMAT " base=%" G_GUINT64_FORMAT,
        rate, pos, seg.base);
    return true;
}

void GStreamerPlayer::start_direct_url_fetch()
{
    if (m_live || m_url.empty())
        return;
    if (m_url_fetch_pid > 0)
        return; /* one fetch at a time */

    /* Drop any URL left behind by a previous video so seek_to_ms() can never
     * read a stale result while the new fetch is still writing the file. */
    if (!m_direct_url_file.empty())
    {
        unlink(m_direct_url_file.c_str());
        m_direct_url_file.clear();
    }

    YtDlpHelper helper(m_ytdlp_binary);
    m_direct_url_file = helper.launch_direct_url_fetch(m_url, m_max_height, &m_url_fetch_pid);
}

void GStreamerPlayer::cancel_direct_url_fetch()
{
    if (m_url_fetch_pid > 0)
    {
        kill(m_url_fetch_pid, SIGTERM);
        waitpid(m_url_fetch_pid, nullptr, 0);
        m_url_fetch_pid = -1;
    }
    if (!m_direct_url_file.empty())
    {
        unlink(m_direct_url_file.c_str());
        m_direct_url_file.clear();
    }
}

bool GStreamerPlayer::seek_to_ms(int64_t ms)
{
    if (!m_started || m_live)
        return false;
    if (ms < 0)
        ms = 0;

    /* 1) Collect the direct (progressive/DASH pair) URL(s). They are usually
     *    already cached by the background fetch started with playback; wait for
     *    them if not. */
    std::string video_url, audio_url;
    if (!YtDlpHelper::read_direct_urls(m_direct_url_file, &video_url, &audio_url))
    {
        if (m_url_fetch_pid > 0)
        {
            for (int i = 0; i < 100 && m_url_fetch_pid > 0; i++) /* up to ~5s */
            {
                int status = 0;
                if (waitpid(m_url_fetch_pid, &status, WNOHANG) == m_url_fetch_pid)
                {
                    m_url_fetch_pid = -1;
                    break;
                }
                usleep(50 * 1000);
            }
        }
        YtDlpHelper::read_direct_urls(m_direct_url_file, &video_url, &audio_url);
    }
    else if (m_url_fetch_pid > 0)
    {
        waitpid(m_url_fetch_pid, nullptr, WNOHANG);
        m_url_fetch_pid = -1;
    }

    if (video_url.empty())
    {
        DBG("seek: no direct stream URL available");
        return false;
    }

    /* 2) Stop the current stream and release the pipeline. */
    stop();

    /* 3) Restart the byte source at the target offset with ffmpeg. The mkv is
     *    re-based to zero, so m_start_offset_ms maps it back to media time. */
    const int64_t target = ms;
    YtDlpHelper helper(m_ytdlp_binary);
    const int fd = helper.launch_seek_stream(video_url, audio_url, target, &m_child_pid);
    if (fd < 0)
    {
        /* Keep the previous playback going when the seek cannot start. */
        m_error = "Failed to launch ffmpeg for seeking";
        start(m_url, false, m_max_height, m_ytdlp_binary);
        return false;
    }
    m_pipe_fd = fd;
    m_ytdlp_log = helper.last_stderr_log();
    m_saw_frame = false;
    m_start_offset_ms = target;

    if (!start_pipeline())
    {
        teardown();
        terminate_child(m_child_pid);
        m_child_pid = -1;
        if (m_pipe_fd >= 0) { close(m_pipe_fd); m_pipe_fd = -1; }
        return false;
    }

    /* 4) Refresh the cached URL for the next seek (YouTube URLs expire). */
    start_direct_url_fetch();

    /* 5) Restore the playback rate chosen by the user (the new pipeline starts
     *    at 1x; the UI re-applies the rate once the first frame arrives). */
    if (m_speed != 1.0)
        m_speed = 1.0;

    DBG("seek_to_ms(%" G_GINT64_FORMAT "): restarted stream at offset", target);
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
        /* Caps are not negotiated yet; wait for them via the notify::caps signal. */
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
        /* decodebin -> queue -> videoconvert -> videoscale -> capsfilter(RGBA) -> appsink */
        names[0] = "queue";
        names[1] = "videoconvert";
        names[2] = "videoscale";
        names[3] = "capsfilter";
    }
    else
    {
        /* decodebin -> queue -> audioconvert -> volume -> audioresample -> autoaudiosink.
         * The audio decoder emits non-interleaved raw audio while volume (and
         * most of the chain) only handles interleaved, so audioconvert goes first
         * to convert the layout at runtime. */
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
        /* Apply the current volume to the new branch (and remember the element so
         * later volume changes reach it too). */
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

    /* Link the decodebin source pad to the start of the branch BEFORE syncing
     * state. A chain that is not fed from upstream would otherwise never
     * complete preroll (e.g. an orphaned autoaudiosink blocks the whole
     * pipeline). If the link fails, remove the orphaned elements again so the
     * rest of the pipeline can still start. */
    GstPad* sinkpad = gst_element_get_static_pad(chain[0], "sink");
    bool linked = false;
    if (sinkpad != nullptr)
    {
        /* Link without a caps check: the decodebin source pad can be exposed
         * before its caps land on it, and the downstream caps query would force
         * an interleaved layout that clashes with the decoder's non-interleaved
         * output (audioconvert converts the layout at runtime instead). */
        GstPadLinkReturn ret =
            gst_pad_link_full(pad, sinkpad, (GstPadLinkCheck)GST_PAD_LINK_CHECK_NOTHING);
        DBG("link_branch(%s): pad link ret=%d", is_video ? "video" : "audio", (int)ret);
        linked = ret == GST_PAD_LINK_OK;
        if (is_video && dbg_enabled())
        {
            gst_pad_add_probe(
                sinkpad, (GstPadProbeType)GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                +[](GstPad*, GstPadProbeInfo* info, gpointer) -> GstPadProbeReturn {
                    GstEvent* ev = GST_EVENT_CAST(info->data);
                    if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT)
                    {
                        const GstSegment* segp = nullptr;
                        gst_event_parse_segment(ev, &segp);
                        DBG("queue sink SEGMENT: rate=%.2f base=%" G_GUINT64_FORMAT,
                            segp ? segp->rate : -1.0, segp ? segp->base : 0);
                    }
                    return GST_PAD_PROBE_OK;
                },
                nullptr, nullptr);
        }
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

    /* Remember the decodebin source pad (ref'd) so set_playback_speed() can
     * push a rate-changing SEGMENT event onto this branch. */
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
    std::vector<char> pending; /* bytes read from the pipe but not yet pushed */

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
            /* A flush (e.g. a playback-rate seek) is in progress. Keep the bytes
             * and retry; otherwise a rate change would kill the feed. */
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

    /* Keep only the tail so the meaningful error lines survive. */
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

} /* namespace imtube */
