#include "core/GStreamerPlayer.h"
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

    // Flush the pipeline first so a feeder thread blocked in
    // gst_app_src_push_buffer() returns immediately.
    teardown();

    terminate_child(m_child_pid);
    m_child_pid = -1;

    if (m_feeder.joinable())
        m_feeder.join();

    if (m_pipe_fd >= 0)
    {
        close(m_pipe_fd);
        m_pipe_fd = -1;
    }

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
    GstElement* chain[4] = {};
    const char* names[4];

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
        // decodebin -> queue -> audioconvert -> audioresample -> autoaudiosink
        names[0] = "queue";
        names[1] = "audioconvert";
        names[2] = "audioresample";
        names[3] = "autoaudiosink";
    }

    for (int i = 0; i < 4; i++)
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

    gst_bin_add_many(GST_BIN(m_pipeline), chain[0], chain[1], chain[2], chain[3], nullptr);
    gst_element_link_many(chain[0], chain[1], chain[2], chain[3], nullptr);

    if (is_video)
        gst_element_link(chain[3], GST_ELEMENT_CAST(m_appsink));

    for (int i = 0; i < 4; i++)
        gst_element_sync_state_with_parent(chain[i]);

    // Link the decodebin source pad to the start of the branch.
    GstPad* sinkpad = gst_element_get_static_pad(chain[0], "sink");
    if (sinkpad != nullptr)
    {
        const GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
        if (ret != GST_PAD_LINK_OK)
            fprintf(stderr, "[gstreamer] pad link failed for %s branch (%d)\n",
                    is_video ? "video" : "audio", (int)ret);
        gst_object_unref(sinkpad);
    }
}

void GStreamerPlayer::feeder_loop()
{
    constexpr size_t kChunk = 128 * 1024;
    std::vector<char> buf(kChunk);

    while (m_feeder_running)
    {
        const ssize_t n = read(m_pipe_fd, buf.data(), buf.size());
        if (n > 0)
        {
            GstBuffer* gbuf = gst_buffer_new_allocate(nullptr, (gsize)n, nullptr);
            gst_buffer_fill(gbuf, 0, buf.data(), (gsize)n);
            const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), gbuf);
            if (ret != GST_FLOW_OK)
                break;
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
}

bool GStreamerPlayer::pull_frame()
{
    if (m_appsink == nullptr || !m_playing)
        return false;

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
                m_frame_w = info.width;
                m_frame_h = info.height;
                m_frame.assign(map.data, map.data + map.size);
                gst_buffer_unmap(buffer, &map);
                got = true;
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
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_error = err && err->message ? err->message : "Unknown GStreamer error";
            }
            if (err)
                g_error_free(err);
            if (dbg)
                g_free(dbg);
            stop();
        }
        else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS)
        {
            m_eos = true;
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

void GStreamerPlayer::teardown()
{
    if (m_pipeline != nullptr)
    {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc = nullptr;
        m_appsink = nullptr;
    }
    m_frame.clear();
    m_frame_w = 0;
    m_frame_h = 0;
}

} // namespace imtube
