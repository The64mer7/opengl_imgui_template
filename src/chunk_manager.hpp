#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "allocator.hpp"
#include "buffer.hpp"

#include "sparse_voxel_tree.hpp"
#include "terrain_gen.hpp"

using svo_node = svo::sparse_tree_node<uint32_t, uint32_t, 2, uint32_t, 64>;
using svo_tree = svo::SparseVoxelTree<svo_node>;

template <size_t dim_count>
void get_morton_coords(uint64_t morton, uint64_t coords[dim_count], uint64_t bits_per_axis,
                       uint64_t& depth)
{
    for (int i = 0; i < dim_count; i++)
        coords[i] = 0;

    depth = 0;
    uint64_t shift = 0;
    uint64_t chunk_mask = (1ULL << (bits_per_axis * dim_count)) - 1;
    uint64_t bitmask = (1ULL << bits_per_axis) - 1;
    while (morton != 1)
    {
        uint64_t curr = morton & chunk_mask;

        for (int i = 0; i < dim_count; i++)
        {
            coords[i] |= ((curr >> (i * bits_per_axis)) & bitmask) << shift;
        }

        morton = morton >> (bits_per_axis * dim_count);
        shift += bits_per_axis;
    }
    depth = shift / bits_per_axis;
}

class TerrainContree
{
public:
    inline static float root_size = 128.f;
    inline static glm::ivec3 chunk_key = glm::ivec3(0);
    static glm::vec3 chunk_origin() { return glm::vec3(chunk_key) * root_size; }

    static svo::node_creation_status create(svo_tree::NodeCreationContext& ctx)
    {
        uint64_t coords[3];
        uint64_t depth;
        int branch_factor = 2;
        get_morton_coords<3>(ctx.morton_index, coords, branch_factor, depth);

        float node_size = root_size / powf(float(1 << branch_factor), depth);
        glm::vec3 center;
        center.x = node_size * (coords[0] + 0.5f) + chunk_origin().x;
        center.y = node_size * (coords[1] + 0.5f) + chunk_origin().y;
        center.z = node_size * (coords[2] + 0.5f) + chunk_origin().z;

        float d = TerrainGen::map(center);

        int variation = rand() % 32;

        uint32_t c = 0;
        c |= (60 + variation) << 0;
        c |= (150) << 4;
        c |= (32) << 8;

        *ctx.node_data = d > 0 ? c : 0;
        return svo::node_creation_status_create;
    }

    static bool is_homogeneous(svo_tree::HomogeneousContext& ctx)
    {
        uint64_t coords[3];
        uint64_t depth;
        int branch_factor = 2;
        get_morton_coords<3>(ctx.morton_index, coords, branch_factor, depth);

        float node_size = root_size / powf(float(1 << branch_factor), depth);
        glm::vec3 center;
        center.x = node_size * (coords[0] + 0.5f) + chunk_origin().x;
        center.y = node_size * (coords[1] + 0.5f) + chunk_origin().y;
        center.z = node_size * (coords[2] + 0.5f) + chunk_origin().z;
        glm::vec2 minmax = TerrainGen::minmax_map(center, glm::vec3(node_size * 0.5f));
        return TerrainGen::is_uniform(minmax);
    }
};

struct ChunkData
{
    uint32_t root_idx = 0;
    uint32_t num_nodes = 0;

    bool is_valid() { return num_nodes != 0; }
};

class ChunkManager
{
public:
    void create(glm::ivec3 chunk_counts, size_t bytes)
    {
        m_chunk_counts = chunk_counts;
        m_chunk_data.resize(chunk_counts.x * chunk_counts.y * chunk_counts.z);

        m_nodes.resize(bytes / sizeof(svo_node));
        m_manager.create(m_nodes.size());

        m_nodes_buffer.create();
        m_nodes_buffer.allocate(m_nodes.size() * sizeof(svo_node), m_nodes.data(), GL_STREAM_DRAW);

        m_chunk_data_buffer.create();
        m_chunk_data_buffer.allocate(m_chunk_data.size() * sizeof(ChunkData), m_chunk_data.data(),
                                     GL_STREAM_DRAW);
    }

    bool create_chunk(glm::ivec3 chunk)
    {
        svo_tree tree;

        TerrainContree::chunk_key = chunk;
        tree.create(TerrainContree::create, TerrainContree::is_homogeneous, 3);

        offset_t offset;
        if (m_manager.alloc(tree.node_count(), offset))
        {
            memcpy(m_nodes.data(), tree.node_data(), tree.node_count() * sizeof(svo_node));

            ChunkData chunk_data;
            chunk_data.num_nodes = tree.node_count();
            chunk_data.root_idx = offset;
            m_chunk_data[chunk_index(chunk)] = chunk_data;

            m_nodes_buffer.upload(chunk_index(chunk) * sizeof(ChunkData), sizeof(ChunkData),
                                  &chunk_data);
            m_chunk_data_buffer.upload(offset * sizeof(svo_node),
                                       tree.node_count() * sizeof(svo_node), tree.node_data());
            return true;
        }
        return false;
    }

    uint32_t chunk_index(glm::ivec3 chunk_key)
    {
        return chunk_key.x + chunk_key.y * m_chunk_counts.x +
               chunk_key.z * m_chunk_counts.x * m_chunk_counts.y;
    }

private:
    glm::ivec3 m_chunk_counts;
    std::vector<ChunkData> m_chunk_data;
    GpuBuffer m_chunk_data_buffer;

    MemoryManager m_manager;
    std::vector<svo_node> m_nodes;
    GpuBuffer m_nodes_buffer;
};