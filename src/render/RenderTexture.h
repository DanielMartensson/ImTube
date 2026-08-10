#pragma once

#include "imgui.h"

#include <cstdint>

namespace imtube {

// A sampled 2D RGBA8 texture renderable by the ImGui backend
// (imgui_id() is passed to ImGui::Image). Used for video frames and
// thumbnails. All functions must be called from the render thread.
class RenderTexture {
public:
    virtual ~RenderTexture() = default;

    // Upload full-frame RGBA8 pixels (width * height * 4 bytes).
    virtual bool upload(const uint8_t* rgba_pixels) = 0;

    virtual bool valid() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;

    // Opaque handle consumable by ImGui::Image / ImDrawList::AddImage.
    virtual ImTextureID imgui_id() const = 0;
};

} // namespace imtube
