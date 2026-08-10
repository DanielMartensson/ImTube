#pragma once

#include "render/RenderTexture.h"

#include <cstdint>

namespace imtube {

// An OpenGL ES 3.2 RGBA8 texture, rendered by ImGui's GL backend as a plain
// GLuint texture id. Render thread only.
class GlesTexture : public RenderTexture {
public:
    GlesTexture(int width, int height);
    ~GlesTexture() override { destroy(); }

    GlesTexture(const GlesTexture&) = delete;
    GlesTexture& operator=(const GlesTexture&) = delete;

    bool upload(const uint8_t* rgba_pixels) override;
    bool valid() const override { return m_texture != 0; }
    int width() const override { return m_width; }
    int height() const override { return m_height; }
    ImTextureID imgui_id() const override { return (ImTextureID)(intptr_t)m_texture; }

private:
    void destroy();

    uint32_t m_texture = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace imtube
