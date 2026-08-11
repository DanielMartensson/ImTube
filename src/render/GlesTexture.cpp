#include "render/GlesTexture.h"

#include <GLES3/gl3.h>

#include <cstdio>

namespace imtube {

GlesTexture::GlesTexture(int width, int height)
    : m_width(width), m_height(height)
{
    if (m_width <= 0 || m_height <= 0)
        return;

    glGenTextures(1, &m_texture);
    if (m_texture == 0)
        return;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool GlesTexture::upload(const uint8_t* rgba_pixels)
{
    if (m_texture == 0 || rgba_pixels == nullptr)
        return false;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GlesTexture::destroy()
{
    if (m_texture != 0)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_width = 0;
    m_height = 0;
}

} /* namespace imtube */
