#include <glad/gl.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

#include "buffer.hpp"

using uint = uint32_t;
using ivec3 = glm::ivec3;
using vec3 = glm::vec3;

const uint empty = 0;
const uint occupied = 1;
const uint removed = 2;

struct SlotGPU
{
    uint offset;
    uint size;

    SlotGPU() = default;
    SlotGPU(uint offset, uint size) : offset(offset), size(size) {}
};

struct Slot
{
    alignas(16) ivec3 key;
    uint state;
    SlotGPU value;
};

inline uint hash_32(uint x)
{
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

inline bool g_cheap_hash = true;

inline uint hash(ivec3 key)
{
    uint h = 0;
    if (!g_cheap_hash)
    {
        uint k = key.x;
        k *= 0xccdad1f1;
        k = (k << 11) | (k >> 21);
        k *= 0xdabd16df;

        h ^= k;
        h *= 0x594fabc1;
        h ^= (h << 15) | (h >> 17);

        k = key.y;
        k *= 0xccdad1f1;
        k = (k << 11) | (k >> 21);
        k *= 0xdabd16df;

        h ^= k;
        h *= 0x594fabc1;
        h ^= (h << 15) | (h >> 17);

        k = key.z;
        k *= 0xccdad1f1;
        k = (k << 11) | (k >> 21);
        k *= 0xdabd16df;

        h ^= k;
        h *= 0x594fabc1;
        h ^= (h << 15) | (h >> 17);
    }
    else
    {
        h = ((key.x * 0x1915dacf + 0x191adf3) ^ (key.y * 0x195adcc1 + 0x0951cadf) ^
             (key.z * 0x191122ae) + 0x191adb12) *
            0x1951fac1;
    }
    return h;
}

struct FixedHashMap
{
    uint allocated_element_count;
    float max_load_factor;
    std::vector<Slot> data;

    uint dirty_index_begin;
    uint dirty_index_end;
    uint removed_count;

    uint size() const { return data.size(); }

    void create(uint max_entry_count)
    {
        data.resize(max_entry_count);
        max_load_factor = 0.75f;
        allocated_element_count = 0;
        dirty_index_begin = 0;
        dirty_index_end = max_entry_count;
        removed_count = 0;

        for (int i = 0; i < size(); i++)
            data[i].state = empty;
    }

    uint hash_index(ivec3 key) const { return hash(key) % size(); }

    bool find(ivec3 key, SlotGPU& out_value) const
    {
        uint offset = hash_index(key);

        for (int i = 0; i < size(); i++)
        {
            uint index = (i + offset) % size();
            Slot slot = data[index];

            if (slot.state == empty)
                return false;

            if (slot.state == occupied && slot.key == key)
            {
                out_value = slot.value;
                return true;
            }
        }
        return false;
    }

    bool insert(ivec3 key, SlotGPU value)
    {
        if (allocated_element_count > size() * max_load_factor)
            return false;

        uint offset = hash_index(key);
        uint first_removed_index = size();

        for (uint i = 0; i < size(); i++)
        {
            uint index = (offset + i) % size();
            if (data[index].state == empty)
            {
                uint target_index = first_removed_index != size() ? first_removed_index : index;
                allocated_element_count++;
                data[target_index].state = occupied;
                data[target_index].key = key;
                data[target_index].value = value;

                dirty_index_begin = glm::min(target_index, dirty_index_begin);
                dirty_index_end = glm::max(target_index + 1, dirty_index_end);
                return true;
            }

            if (data[index].state == occupied && data[index].key == key)
            {
                data[index].value = value;
                dirty_index_begin = glm::min(index, dirty_index_begin);
                dirty_index_end = glm::max(index, dirty_index_end);
                return true;
            }

            if (data[index].state == removed && first_removed_index == size())
                first_removed_index = index;
        }

        return false;
    }

    bool remove(ivec3 key)
    {
        uint offset = hash_index(key);
        for (uint i = 0; i < size(); i++)
        {
            uint index = (offset + i) % size();

            if (data[index].state == empty)
                return false;

            if (data[index].state == occupied && key == data[index].key)
            {
                dirty_index_begin = glm::min(index, dirty_index_begin);
                dirty_index_end = glm::max(index, dirty_index_end);

                allocated_element_count--;
                data[index].state = removed;
                removed_count++;
                return true;
            }
        }
        return false;
    }

    float load_factor() const { return (removed_count + allocated_element_count) / float(size()); }

    bool should_rehash() const
    {
        return (removed_count + allocated_element_count) >= max_load_factor * size();
    }

    void rehash(uint32_t rehash_size = 0)
    {
        FixedHashMap new_map;
        new_map.create(rehash_size == 0 ? data.size() : rehash_size);

        for (auto& slot : data)
        {
            if (slot.state == occupied)
                new_map.insert(slot.key, slot.value);
        }
        *this = std::move(new_map);
    }
};

struct GpuHashMap
{
    GpuBuffer buffer;
    uint capacity = 0;

    void create(uint max_entry_count)
    {
        buffer.Create();
        capacity = max_entry_count;
        uint metadata_size = 16u;
        buffer.Allocate(metadata_size + max_entry_count * sizeof(Slot), nullptr, GL_STREAM_DRAW);
    }

    uint upload(FixedHashMap& hash_map)
    {
        if (hash_map.dirty_index_begin == hash_map.dirty_index_end)
            return 0;

        struct Metadata
        {
            uint size;
            uint allocated_element_count;
            float max_load_factor;
        } map;

        map.allocated_element_count = hash_map.allocated_element_count;
        map.max_load_factor = hash_map.max_load_factor;
        map.size = hash_map.size();

        uint size = sizeof(map.allocated_element_count);
        buffer.Upload(0, size, &map.allocated_element_count);

        size = sizeof(map.max_load_factor);
        buffer.Upload(4, size, &map.max_load_factor);

        size = sizeof(map.size);
        buffer.Upload(8, size, &map.size);

        size = (hash_map.dirty_index_end - hash_map.dirty_index_begin) * sizeof(Slot);
        buffer.Upload(16 + hash_map.dirty_index_begin * sizeof(Slot), size,
                      hash_map.data.data() + hash_map.dirty_index_begin);

        uint uploaded_size = 16 + size;

        hash_map.dirty_index_begin = 0;
        hash_map.dirty_index_end = 0;

        return uploaded_size;
    }

    void destroy() { buffer.cleanup(); }
};
