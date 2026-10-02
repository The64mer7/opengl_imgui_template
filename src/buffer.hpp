#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>

inline uint32_t memory_exponent(uint64_t size)
{
    return glm::log2(float(size)) / glm::log2(1024.f);
}

inline float memory_normalized(uint64_t size, uint32_t exp)
{
    return size / glm::pow(1024.f, float(exp));
}

struct GpuBufferMapping
{
    void* buffer;
    size_t offset;
    size_t size;
};

class GpuBuffer
{
public:
    GpuBuffer() = default;
    ~GpuBuffer() { destroy(); }

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    void name(const std::string& buffer_name) // for renderdoc
    {
        glObjectLabel(GL_BUFFER, m_handle, buffer_name.size(), buffer_name.c_str());
    }

    void create() { glCreateBuffers(1, &m_handle); }

    void create(const std::string& buffer_name)
    {
        create();
        name(buffer_name);
    }

    void allocate_storage(size_t size, void* data, GLbitfield flags)
    {
        glNamedBufferStorage(m_handle, size, data, flags);
        m_size = size;
        total_bytes += size;
    }

    void allocate(size_t size, void* data, GLenum usage)
    {
        glNamedBufferData(m_handle, size, data, usage);
        m_size = size;
    }

    void upload(GLintptr offset, GLsizeiptr size, const void* data)
    {
        glNamedBufferSubData(m_handle, offset, size, data);
    }

    void shift_data(size_t write_offset, size_t read_offset, size_t size)
    {
        void* ptr = glMapNamedBuffer(m_handle, GL_READ_WRITE);
        if (!ptr)
            return;

        char* bytes_ptr = reinterpret_cast<char*>(ptr);
        memmove(bytes_ptr + write_offset, bytes_ptr + read_offset, size);
        glUnmapNamedBuffer(m_handle);
    }

    GpuBufferMapping map()
    {
        GpuBufferMapping mapping;
        mapping.buffer = glMapNamedBufferRange(
            m_handle, 0, m_size, GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT | GL_MAP_PERSISTENT_BIT);
        mapping.offset = 0;
        mapping.size = m_size;
        return mapping;
    }

    void unmap() { glUnmapNamedBuffer(m_handle); }

    void destroy()
    {
        if (m_handle != 0)
        {
            glDeleteBuffers(1, &m_handle);
            m_handle = 0;
        }
    }

    GLuint handle() const { return m_handle; }
    size_t size_in_bytes() const { return m_size; }

private:
    GLuint m_handle = 0;
    size_t m_size = 0;
    inline static uint64_t total_bytes = 0;
};