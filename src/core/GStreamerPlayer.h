#pragma once

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

namespace imtube {

// Plays a video/audio stream with GStreamer.
//
// The stream is fed into the pipeline from the stdout of a yt-dlp subprocess
// (see YtDlpHelper::launch_stream) through an appsrc element. A worker thread
// performs the pipe->appsrc transfer with natural backpressure, while a
// decodebin branches into:
//   video -> queue ! videoconvert ! videoscale ! appsink (video/x-raw,RGBA)
//   audio -> queue ! audioconvert ! audioresample ! autoaudiosink
//
// The main thread pulls the newest decoded RGBA frame (appsink keeps only one
// buffer and drops stale ones), uploads it into a Vulkan texture and lets the
// UI render it. This keeps the whole playback stack self-contained, portable
// (PC Linux and STM32MP25 / Yocto) and independent of any windowing backend.
class GStreamerPlayer {
public:
    GStreamerPlayer() = default;
    ~GStreamerPlayer() { stop(); }

    GStreamerPlayer(const GStreamerPlayer&) = delete;
    GStreamerPlayer& operator=(const GStreamerPlayer&) = delete;

    // Initialize the GStreamer library once per process. Thread-safe.
    static bool ensure_gst_initialized();

    // Start streaming. Fails fast if yt-dlp could not be launched.
    bool start(const std::string& url, bool live, int max_height,
               const std::string& ytdlp_binary = "yt-dlp");

    // Stop playback, kill the yt-dlp child and release the pipeline.
    void stop();

    void set_paused(bool paused);
    bool toggle_pause();

    // Non-blocking: copies the newest decoded frame into an internal buffer.
    // Returns true when a fresh frame is available (frame_pixels() != nullptr).
    bool pull_frame();

    const uint8_t* frame_pixels() const { return m_frame.empty() ? nullptr : m_frame.data(); }
    int frame_width() const { return m_frame_w; }
    int frame_height() const { return m_frame_h; }

    // Process pending bus messages (EOS/ERROR). Call from the main thread.
    void pump_bus();

    bool is_playing() const { return m_playing; }
    bool is_paused() const { return m_paused; }
    bool is_live() const { return m_live; }
    bool has_eos() const { return m_eos; }
    std::string error() const;

private:
    void on_pad_added(GstElement* decodebin, GstPad* pad);
    void link_branch(GstPad* pad, bool is_video);

    void feeder_loop();
    void teardown();

    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    GstAppSink* m_appsink = nullptr;

    int m_pipe_fd = -1;
    pid_t m_child_pid = -1;

    std::thread m_feeder;
    std::atomic<bool> m_feeder_running{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_eos{false};
    std::atomic<bool> m_started{false};
    bool m_live = false;

    mutable std::mutex m_mutex;
    std::string m_error;

    // Latest decoded RGBA frame (owned here; written by the main thread).
    std::vector<uint8_t> m_frame;
    int m_frame_w = 0;
    int m_frame_h = 0;
};

} // namespace imtube
