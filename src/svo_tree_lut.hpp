#pragma once

#include <cstdint>
#include <glm/glm.hpp>

inline glm::vec3 get_safe_ird(const glm::vec3& rd)
{
    glm::vec3 safe_sign = glm::mix(glm::vec3(-1.0f), glm::vec3(1.0f),
                                   glm::greaterThanEqual(rd, glm::vec3(0.0f)));
    glm::vec3 safe_abs_rd = glm::max(glm::vec3(1e-5f), glm::abs(rd));
    return safe_sign / safe_abs_rd;
}
inline bool ray_aabb_intersect(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& aabb_min,
                               const glm::vec3& aabb_max)
{
    glm::vec3 ird = get_safe_ird(rd);

    glm::vec3 tmin = (aabb_min - ro) * ird;
    glm::vec3 tmax = (aabb_max - ro) * ird;

    glm::vec3 t_entries = glm::min(tmin, tmax);
    glm::vec3 t_exits = glm::max(tmin, tmax);

    float t_entry = glm::max(t_entries.x, glm::max(t_entries.y, t_entries.z));
    float t_exit = glm::min(t_exits.x, glm::min(t_exits.y, t_exits.z));

    t_entry = glm::max(t_entry, 0.0f);
    return t_entry <= t_exit;
}

class ContreeBitmaskLUT
{
public:
    void create()
    {
        for (int z0 = 0; z0 < 4; z0++)
            for (int y0 = 0; y0 < 4; y0++)
                for (int x0 = 0; x0 < 4; x0++)
                    for (int z1 = 0; z1 < 4; z1++)
                        for (int y1 = 0; y1 < 4; y1++)
                            for (int x1 = 0; x1 < 4; x1++)
                                create_entry({x0, y0, z0}, {x1, y1, z1});
    }
    uint64_t get(const glm::ivec3& begin_coord, const glm::ivec3& end_coord)
    {
        return m_data[index(begin_coord)][index(end_coord)];
    }
    uint64_t* get_data() { return &m_data[0][0]; }
    int index(const glm::ivec3& coord) { return coord.x + coord.y * 4 + coord.z * 16; }

private:
    void create_entry(const glm::ivec3& p0, const glm::ivec3& p1)
    {

        m_data[index(p0)][index(p1)] = 0;

        for (int z = 0; z < 2; z++)
            for (int y = 0; y < 2; y++)
                for (int x = 0; x < 2; x++)
                {
                    glm::vec3 wp0 = glm::vec3(p0) + 0.125f +
                                    glm::vec3{x, y, z} * (1.f - 2.f * 0.125f);
                    glm::vec3 wp1 = glm::vec3(p1) + 0.125f +
                                    glm::vec3{x, y, z} * (1.f - 2.f * 0.125f);
                    m_data[index(p0)][index(p1)] |= get_bitmask(wp0, wp1 - wp0);
                }
    }

    uint64_t get_bitmask(glm::vec3 ro, glm::vec3 rd)
    {
        uint64_t bitmask = 0;
        for (int z = 0; z < 4; z++)
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                {
                    glm::vec3 box_min = {x, y, z};
                    glm::vec3 box_max = box_min + 1.f;
                    if (ray_aabb_intersect(ro, rd, box_min, box_max))
                        bitmask |= uint64_t(1) << index({x, y, z});
                }

        return bitmask;
    }

    uint64_t m_data[4 * 4 * 4][4 * 4 * 4];
};
