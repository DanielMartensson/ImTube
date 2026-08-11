#pragma once

#include "core/YtDlpHelper.h" // VideoItem

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace imtube {

// A decoded RGBA8 thumbnail ready to be uploaded into a GPU texture on the
// render thread.
struct DecodedThumbnail {
    std::string video_id;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

// Fetches video thumbnails off the render thread.
//
// A single worker thread downloads the image data (libcurl, from
// item.thumbnail_url or a derived i.ytimg.com URL) and decodes it (stb_image)
// to RGBA8. Completed thumbnails are queued and consumed by the main thread via
// poll(), which is the only thread allowed to upload into Vulkan textures.
//
// Thumbnails are cached on disk (~/.cache/imtube/thumbs/<id>.jpg) so repeated
// searches do not hit the network again.
class ThumbnailLoader {
public:
    ThumbnailLoader();
    ~ThumbnailLoader() { shutdown(); }

    ThumbnailLoader(const ThumbnailLoader&) = delete;
    ThumbnailLoader& operator=(const ThumbnailLoader&) = delete;

    // Enqueue the thumbnail for each item (skips ids that are already known).
    void request_thumbnails(const std::vector<VideoItem>& items);

    // Non-blocking. Returns one decoded thumbnail when available.
    bool poll(DecodedThumbnail& out);

    void shutdown();

private:
    void worker_loop();
    void load_one(const VideoItem& item);

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<VideoItem> m_pending;
    std::unordered_set<std::string> m_known;
    std::queue<DecodedThumbnail> m_ready;

    std::string m_cache_dir;
};

} // namespace imtube
