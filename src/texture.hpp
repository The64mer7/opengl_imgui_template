#pragma once
#include <cstdint>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

struct Texture
{
    GLuint handle;
    GLenum target;
    GLenum internalFormat;

    void Create() { glCreateTextures(target, 1, &handle); }
    void Allocate(uint64_t width, uint64_t height)
    {
        m_Width = width;
        m_Height = height;

        glTextureStorage2D(handle, 1, internalFormat, width, height);

        glTextureParameteri(handle, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(handle, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    void Upload(GLenum format, GLenum type, void* data)
    {
        glTextureSubImage2D(handle, 0, 0, 0, Width(), Height(), format, type, data);
    }
    void Delete()
    {
        glDeleteTextures(1, &handle);
        handle = 0;
    }
    uint64_t Width() { return m_Width; }
    uint64_t Height() { return m_Height; }

private:
    uint64_t m_Width = 0;
    uint64_t m_Height = 0;
};