#pragma once
#include "glm/glm.hpp"
#include "svo/sparse_voxel_tree.h"
#include "svo_lut.h"
#include <cstdint>
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtx/component_wise.hpp"

template <typename DataT>
using tree16_node = svo::sparse_tree_node<DataT, uint16_t, 1, uint32_t, 16>;
;
using svo_node = tree16_node<uint32_t>;
using svo_tree = svo::SparseVoxelTree<svo_node>;

struct raymarch_hit
{
    inline static const float t_miss = -1.f;

    float t;
    uint32_t data;
};

glm::vec2 get_safe_ird(glm::vec2 rd)
{
    glm::vec2 safe_sign = glm::vec2(glm::greaterThanEqual(rd, glm::vec2(0.f))) * 2.f - 1.f;
    glm::vec2 safe_abs_rd = glm::max(glm::vec2(1e-5f), glm::abs(rd));
    return safe_sign / safe_abs_rd;
}

glm::vec2 get_min_mask(glm::vec2 v)
{
    return glm::lessThanEqual(glm::vec2{v.x, v.y}, glm::vec2{v.y, v.x});
}

void advance_plane(glm::vec2* plane, glm::vec2* side_dist, glm::ivec2* map, glm::vec2 srd,
                   glm::vec2 delta_dist, float size)
{
    glm::vec2 min_mask = get_min_mask(*side_dist);
    *plane += min_mask * size * srd;
    *side_dist += min_mask * size * delta_dist;
    *map += min_mask * srd;
}

uint32_t flatten_index(glm::ivec2 c, int axis_branching_factor)
{
    return c.x + c.y * axis_branching_factor;
}

glm::ivec2 coord_from_world(glm::vec2 wp, float size) { return glm::floor(wp / size); }

glm::vec2 quantize_world(glm::vec2 wp, float size) { return glm::floor(wp / size) * size; }

template <glm::length_t L>
glm::vec<L, int> get_local_coord(glm::vec<L, int> coord)
{
    return coord % 4;
}

glm::ivec2 get_child_coord(glm::ivec2 coord, glm::ivec2 local, int axis_branching_factor)
{
    return coord * axis_branching_factor + local;
}

glm::ivec2 get_parent_coord(glm::ivec2 coord, int axis_branching_factor)
{
    return coord / axis_branching_factor;
}

glm::vec2 plane_from_coord(glm::ivec2 coord, float size, glm::ivec2 pos_rd)
{
    return glm::vec2(coord + pos_rd) * size;
}

glm::vec2 plane_dist(glm::vec2 plane_pos, glm::vec2 ro, glm::vec2 ird)
{
    return (plane_pos - ro) * ird;
}

glm::vec2 pos_along_ray(glm::vec2 ro, glm::vec2 rd, float t) { return ro + rd * t; }

ContreeBitmaskLUT& get_lut()
{
    static bool first = true;
    static ContreeBitmaskLUT lut;
    if (first)
    {
        lut.create();
        first = false;
    }
    return lut;
}

raymarch_hit raymarch_tree(svo_tree& tree, float tree_size, glm::vec2 ro, glm::vec2 rd)
{
    svo_node* nodes = tree.node_data();
    uint32_t num_nodes = tree.node_count();

    raymarch_hit hit = {};

    uint32_t node_idx = 0;

    glm::vec2 ird = get_safe_ird(rd);
    glm::vec2 delta_dist = glm::abs(ird);
    glm::ivec2 srd = glm::sign(rd);
    glm::vec2 pos_rd = glm::step(glm::vec2(0.f), rd);

    glm::ivec2 coord = {0, 0};
    uint32_t depth = 0;

    int axis_branching_factor = 4;

    float node_size = tree_size;

    glm::vec2 plane_pos = glm::floor(ro / node_size + pos_rd) * node_size;
    coord = glm::floor(ro / node_size);

    glm::vec2 side_dist(0.f);

    auto traverse_forward = [&](svo_node& node)
    {
        glm::ivec2 entry = (coord / axis_branching_factor) * axis_branching_factor;
        glm::ivec2 exit = entry + axis_branching_factor;

        glm::ivec2 parent_coord = get_parent_coord(coord, axis_branching_factor);
        glm::ivec2 parent_dir_coord = parent_coord + glm::ivec2(pos_rd);
        glm::ivec2 exit_child_coord = parent_dir_coord * axis_branching_factor;
        glm::vec2 exit_plane_pos = glm::vec2(exit_child_coord) * node_size;
        glm::vec2 exit_side_dist = plane_dist(exit_plane_pos, ro, ird);
        float min_dist = glm::compMin(exit_side_dist);

        glm::vec2 inv_normal = -get_min_mask(exit_side_dist) * glm::vec2(srd) * node_size / 128.f;
        glm::vec2 exit_pos = pos_along_ray(ro, rd, min_dist);

        glm::ivec2 exit_coord = coord_from_world(exit_pos + inv_normal, node_size);

        uint16_t mask = node.childmask.mask[0];
        uint16_t potential_children = get_lut().get(
            get_local_coord(glm::ivec3{coord.x, coord.y, 0}),
            get_local_coord(glm::ivec3{exit_coord.x, exit_coord.y, 0}));

        if ((mask & potential_children) == 0)
        {
            return svo_tree::invalid_offset;
        }

        while (glm::all(glm::greaterThanEqual(coord, entry)) &&
               glm::all(glm::lessThan(coord, exit)))
        {
            glm::vec2 min_mask = get_min_mask(side_dist);
            glm::ivec2 next_coord = coord + glm::ivec2(min_mask) * srd;

            if (glm::any(glm::lessThan(next_coord, entry)) ||
                glm::any(glm::greaterThanEqual(next_coord, exit)))
                break;

            uint32_t bit_index = flatten_index(get_local_coord(next_coord), axis_branching_factor);
            if (node.get_bitmask(bit_index))
            {
                coord = next_coord;
                uint32_t child_index = svo::get_child_ptr(node, bit_index);
                return child_index;
            }

            advance_plane(&plane_pos, &side_dist, &coord, srd, delta_dist, node_size);
        }
        return svo_tree::invalid_offset;
    };

    auto descend = [&](glm::vec2 p)
    {
        while (node_idx < num_nodes && nodes[node_idx].child_count() != 0)
        {
            float child_size = node_size / axis_branching_factor;
            glm::ivec2 global = coord_from_world(p, child_size);
            glm::ivec2 local = get_local_coord(global);
            glm::ivec2 child_coord = get_child_coord(coord, local, axis_branching_factor);

            uint32_t bit_index = flatten_index(local, axis_branching_factor);
            coord = child_coord;
            node_size = child_size;

            if (nodes[node_idx].get_bitmask(bit_index))
            {
                uint32_t child_idx = svo::get_child_ptr(nodes[node_idx], bit_index);
                node_idx = child_idx;
                depth++;
            }
            else
                break;
        }
        plane_pos = plane_from_coord(coord, node_size, pos_rd);
        side_dist = plane_dist(plane_pos, ro, ird);
    };

    auto ascend = [&](uint32_t from_node_idx)
    {
        coord = get_parent_coord(coord, axis_branching_factor);
        node_idx = nodes[from_node_idx].parent;
        node_size *= axis_branching_factor;
        plane_pos = plane_from_coord(coord, node_size, pos_rd);
        side_dist = plane_dist(plane_pos, ro, ird);
        if (depth == 0)
            return false;
        depth--;
        return true;
    };

    printf("BEGIN\n");
    descend(ro);
    float prev_min_dist = 0.f;
    for (int i = 0; i < 16; i++)
    {
        svo_node& node = nodes[node_idx];

        if (node.child_count())
        {
            printf("FORWARD\n");
            uint32_t first_node = traverse_forward(node);
            if (first_node == svo_tree::invalid_offset)
            {
                printf("ASCEND\n");
                if (!ascend(node_idx))
                    break;
                continue;
            }

            node_idx = first_node;
            depth++;

            glm::vec2 min_mask = get_min_mask(side_dist);
            glm::vec2 normal = min_mask * glm::vec2(srd);
            printf("DESCEND\n");
            prev_min_dist = glm::compMin(side_dist);
            descend(normal * node_size * 1.f / 256.f +
                    pos_along_ray(ro, rd, glm::compMin(side_dist)));
        }
        else
        {
            printf("RETURN\n");
            hit.t = prev_min_dist;
            hit.data = node.data;
            return hit;
        }

        if (node_idx == svo_tree::invalid_offset)
            break;
    }

    hit.t = hit.t_miss;
    return hit;
}