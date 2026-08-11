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

    // Current playback position and total duration, in milliseconds.
    // A value of -1 means "unknown" (e.g. live streams / not yet available).
    // Returns false when nothing is playing.
    bool get_position_and_duration(int64_t* pos_ms, int64_t* dur_ms) const;

    // Playback rate (0.5 = half speed ... 2.0 = double). Returns false when the
    // pipeline cannot change speed (e.g. before the stream starts); on success
    // playback_speed() reports the new rate. Forward playback only.
    bool set_playback_speed(double rate);
    double playback_speed() const { return m_speed; }

    // Jumps playback to the given media time by restarting the stream at that
    // offset (the yt-dlp stdout pipe itself is not seekable). Requires a
    // single-file progressive format for the video; returns false when that is
    // not available (e.g. DASH-only videos or live streams). The target is
    // aligned to the nearest preceding keyframe. Blocking (~a second), call
    // from the main thread.
    bool seek_to_ms(int64_t ms);

    // The total media duration in milliseconds, taken from the yt-dlp metadata.
    // A streamed container (matroska from the ffmpeg merge) cannot report its
    // real length until EOS, so the progress bar would otherwise only ever show
    // the end of the video. Pass 0 when unknown (live streams, raw URLs).
    void set_known_duration_ms(int64_t ms) { m_known_duration_ms = ms; }

    void set_volume(float volume);   // 0..1
    float volume() const { return m_volume; }
    void set_muted(bool muted);
    bool is_muted() const { return m_muted; }
    void toggle_mute() { set_muted(!m_muted); }

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
    bool start_pipeline();
    void start_direct_url_fetch();
    void cancel_direct_url_fetch();

    // Appends the tail of the last yt-dlp stderr log to m_error (called when
    // playback fails before/during decode, so the real cause shows up in the UI).
    void append_ytdlp_error(const char* prefix);

    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    GstAppSink* m_appsink = nullptr;
    GstElement* m_volume_elem = nullptr;

    int m_pipe_fd = -1;
    pid_t m_child_pid = -1;

    std::thread m_feeder;
    std::atomic<bool> m_feeder_running{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_eos{false};
    int64_t m_known_duration_ms = 0;
    std::atomic<bool> m_started{false};
    bool m_live = false;

    // Playback state
    double m_speed = 1.0;
    float m_volume = 1.0f;
    bool m_muted = false;

    // Stream parameters, kept so seek_to_ms() can restart playback.
    std::string m_url;
    int m_max_height = 0;
    std::string m_ytdlp_binary = "yt-dlp";

    // Media-time offset of the current byte source (0 for a normal stream,
    // the seek target for a restarted ffmpeg segment). Added to all
    // position/duration queries so the UI shows real media time.
    int64_t m_start_offset_ms = 0;

    // Background "yt-dlp -g" prefetch: gives seek_to_ms() the direct URL of a
    // single-file progressive stream without blocking the UI.
    int m_url_fetch_pid = -1;
    std::string m_direct_url_file;

    // Rate-change state. The stream from yt-dlp's stdout is not seekable, so
    // the rate is changed by pushing a fresh TIME segment onto the decoded
    // branch pads (never into decodebin, whose demuxer would restart). The
    // running time is carried across changes in m_segment.base.
    std::vector<GstPad*> m_branch_pads; // ref'd decodebin source pads
    GstSegment m_segment = {};          // stream time -> running time bookkeeping

    // Diagnostics: path of the yt-dlp stderr log and whether a video frame was
    // ever decoded (used to tell "yt-dlp failed" apart from real end-of-stream).
    std::string m_ytdlp_log;
    std::atomic<bool> m_saw_frame{false};
    uint64_t m_push_count = 0;

    mutable std::mutex m_mutex;
    std::string m_error;

    // Latest decoded RGBA frame (owned here; written by the main thread).
    std::vector<uint8_t> m_frame;
    int m_frame_w = 0;
    int m_frame_h = 0;
};

} // namespace imtube
