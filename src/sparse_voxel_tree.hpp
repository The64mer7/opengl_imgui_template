#pragma once

#include <bit>
#include <format>
#include <iostream>
#include <queue>
#include <vector>

#ifndef SVO_FREELIST_MAX_ALLOC_COUNT
#define SVO_FREELIST_MAX_ALLOC_COUNT 64
#endif

namespace svo
{
template <typename T, size_t N>
struct bitmask
{
    T mask[N];

    constexpr void reset(size_t i) { mask[elem_index(i)] &= ~(T(1) << bit_index(i)); }

    constexpr void set(size_t i) { mask[elem_index(i)] |= T(1) << bit_index(i); }

    constexpr bool get(size_t i) const
    {
        return 0 != (mask[elem_index(i)] & (T(1) << bit_index(i)));
    }

    constexpr size_t set_count() const
    {
        size_t count = 0;
        for (size_t i = 0; i < N; i++)
            count += std::popcount(mask[i]);
        return count;
    }

    constexpr void clear()
    {
        for (size_t i = 0; i < N; i++)
            mask[i] = T(0);
    }

    constexpr size_t elem_index(size_t i) const { return i / bits_per_elem(); }

    constexpr static size_t bit_index(size_t i) { return i % bits_per_elem(); }

    constexpr static size_t bits_per_elem() { return (sizeof(T) * 8); }

    constexpr static size_t find_lsb_none = N * bits_per_elem();
    constexpr size_t find_lsb(size_t start_i) const
    {
        size_t elem_idx = elem_index(start_i);
        size_t bit_offset = bit_index(start_i);

        T current_mask = mask[elem_idx];
        if (bit_offset > 0)
        {
            current_mask &= (~T(0) << bit_offset);
        }

        if (current_mask != T(0))
        {
            return (elem_idx * bits_per_elem()) + std::countr_zero(current_mask);
        }

        for (size_t i = elem_idx + 1; i < N; ++i)
        {
            if (mask[i] != T(0))
            {
                return (i * bits_per_elem()) + std::countr_zero(mask[i]);
            }
        }

        return find_lsb_none;
    }

    constexpr size_t count_before(size_t i) const
    {
        const size_t elem_idx = elem_index(i);
        const size_t bit_offset = bit_index(i);

        size_t count = 0;

        for (size_t i = 0; i < elem_idx; ++i)
            count += glm::bitCount(mask[i]);

        if (bit_offset != 0)
            count += glm::bitCount(mask[elem_idx] & ((T(1) << bit_offset) - 1));

        return count;
    }
};

template <typename OffsetT, size_t max_alloc_size>
class FreeList
{
public:
    using offset_t = OffsetT;

    constexpr size_t get_max_alloc_size() { return max_alloc_size; }

    struct OverflowBlock
    {
        offset_t offset;
        offset_t size;
    };

    inline static constexpr offset_t null = std::numeric_limits<offset_t>::max();
    offset_t get_free(size_t size)
    {
        size_t free_index = m_bitmask.find_lsb(size - 1);
        if (free_index == m_bitmask.find_lsb_none)
            return get_overflow_free(size);

        offset_t fast_free_offset = m_fast_free[free_index][m_fast_free_counts[free_index] - 1];
        m_fast_free_counts[free_index]--;

        if (m_fast_free_counts[free_index] == 0)
            m_bitmask.reset(free_index);

        size_t free_size = free_index + 1;
        if (free_size > size)
            insert_free(fast_free_offset + size, free_size - size);

        return fast_free_offset;
    }

    void insert_free(offset_t offset, size_t size)
    {
        if (m_fast_free_counts[size - 1] >= SVO_FREELIST_MAX_ALLOC_COUNT)
        {
            m_overflow.emplace_back(offset, size);
            return;
        }
        if (m_fast_free_counts[size - 1] == 0)
            m_bitmask.set(size - 1);
        m_fast_free[size - 1][m_fast_free_counts[size - 1]] = offset;
        m_fast_free_counts[size - 1]++;
    }

private:
    offset_t get_overflow_free(size_t size)
    {
        for (auto it = m_overflow.begin(); it != m_overflow.end(); ++it)
        {
            OverflowBlock block = *it;
            if (block.size < size)
                continue;

            if (block.size == size)
            {
                *it = m_overflow.back();
                m_overflow.pop_back();
                return block.offset;
            }

            it->offset += size;
            it->size -= size;

            return block.offset;
        }
        return null;
    }

    offset_t m_fast_free[max_alloc_size][SVO_FREELIST_MAX_ALLOC_COUNT];
    size_t m_fast_free_counts[max_alloc_size] = {};

    static constexpr size_t c_word_count = (max_alloc_size + 63) / 64;
    svo::bitmask<uint64_t, c_word_count> m_bitmask;

    std::vector<OverflowBlock> m_overflow;
};

/**
 *
 * @brief a GPU-friendly generic tree node structure.
 *
 * memory layout:
 *
 * [DataT data, ChildmaskT childmask, OffsetT first_child, OffsetT parent]
 *
 *  * members:
 *
 * - childmask: bitmask where each bit indicates presence of a child
 *
 * - firstchild: offset into flat node array
 *               (0xFFFFFFFF if no children or invalid)
 *
 * - parent: offset into flat node array
 *               (0xFFFFFFFF if no children or invalid)
 *
 * @tparam DataT - your custom data (POD recommended)
 * @tparam ChildmaskT - underlying type for children mask
 * @tparam OffsetT - underlying type children and parent offset
 *
 */
template <typename DataT, typename ChildmaskT, size_t ChildmaskN, typename OffsetT,
          size_t MaxChildren>
struct sparse_tree_node
{
    using child_offset_t = OffsetT;
    using child_mask_t = ChildmaskT;
    using data_t = DataT;

    constexpr static size_t max_children = MaxChildren;
    constexpr static size_t morton_length = std::bit_width(max_children - 1);

    data_t data;                                 /// your custom data, supports any POD
    bitmask<child_mask_t, ChildmaskN> childmask; /// nth bit represents nth child presence
    child_offset_t firstchild;                   /// offset into the first child in an array
    child_offset_t parent;                       /// offset into the parent in an array

    inline void reset_bitmask(size_t i) { childmask.reset(i); }

    inline void set_bitmask(size_t i) { childmask.set(i); }

    inline bool get_bitmask(size_t i) { return childmask.get(i); }

    inline void clear_bitmask() { childmask.clear(); }

    inline size_t child_count() { return childmask.set_count(); }

    inline size_t find_lsb(size_t start_i) { return childmask.find_lsb(start_i); }
};

template <typename NodeT>
typename NodeT::child_offset_t get_child_offset(NodeT& node, size_t bit_index)
{
    return node.childmask.count_before(bit_index);
}

template <typename NodeT>
typename NodeT::child_offset_t get_child_ptr(NodeT& node, size_t bit_index)
{
    return node.firstchild + get_child_offset(node, bit_index);
}

/**
 * @brief tree_node variant with 2x32-bit mask and 32-bit index.
 *
 * Equivalent to:
 * sparse_tree_node<DataT, uint32_t, 2, uint32_t, 64>
 *
 * @tparam DataT payload type (POD recommended)
 */
template <typename DataT>
using contree_node = sparse_tree_node<DataT, uint32_t, 2, uint32_t, 64>;

/// 64 bit firstchild offset variant
template <typename DataT>
using contree_node_o64u = sparse_tree_node<DataT, uint64_t, 1, uint64_t, 64>;
/// 32 bit firstchild offset variant
template <typename DataT>
using contree_node_o32u = sparse_tree_node<DataT, uint64_t, 1, uint32_t, 64>;
/// 16 bit firstchild offset variant
template <typename DataT>
using contree_node_o16u = sparse_tree_node<DataT, uint64_t, 1, uint16_t, 64>;
/// 8 bit firstchild offset variant
template <typename DataT>
using contree_node_o8u = sparse_tree_node<DataT, uint64_t, 1, uint8_t, 64>;

/// 64 bit firstchild offset variant
template <typename DataT>
using octree_node_o64u = sparse_tree_node<DataT, uint8_t, 1, uint64_t, 8>;
/// 32 bit firstchild offset variant
template <typename DataT>
using octree_node_o32u = sparse_tree_node<DataT, uint8_t, 1, uint32_t, 8>;
/// 16 bit firstchild offset variant
template <typename DataT>
using octree_node_o16u = sparse_tree_node<DataT, uint8_t, 1, uint16_t, 8>;
/// 8 bit firstchild offset variant
template <typename DataT>
using octree_node_o8u = sparse_tree_node<DataT, uint8_t, 1, uint8_t, 8>;

template <typename DataT>
using quadtree_node = sparse_tree_node<DataT, uint8_t, 1, uint32_t, 4>;
;

/// 64 bit firstchild offset variant
template <typename DataT>
using quadtree_node_o64u = sparse_tree_node<DataT, uint8_t, 1, uint64_t, 4>;
/// 32 bit firstchild offset variant
template <typename DataT>
using quadtree_node_o32u = sparse_tree_node<DataT, uint8_t, 1, uint32_t, 4>;
/// 16 bit firstchild offset variant
template <typename DataT>
using quadtree_node_o16u = sparse_tree_node<DataT, uint8_t, 1, uint16_t, 4>;
/// 8 bit firstchild offset variant
template <typename DataT>
using quadtree_node_o8u = sparse_tree_node<DataT, uint8_t, 1, uint8_t, 4>;

enum tree_return_status
{
    tree_return_status_created,
    tree_return_status_not_created,
    tree_return_status_error
};

enum node_creation_status
{
    node_creation_status_create,
    node_creation_status_stop,
    node_creation_status_error
};

/**
 * @brief flat tree generator
 *
 */
template <typename NodeT>
class SparseVoxelTree
{
public:
    // using NodeT = contree_node<int>;
    using offset_t = NodeT::child_offset_t;
    using child_mask_t = NodeT::child_mask_t;

    struct NodeCreationContext
    {
        uint64_t morton_index;             /// index of a new node (1) for root
        NodeT* parent;                     /// pointer to the parent, nullptr if creating root
        typename NodeT::data_t* node_data; /// always valid pointer for new node data
        uint8_t current_depth;             /// depth of the current child (0 for root)
    };

    struct HomogeneousContext
    {
        uint64_t morton_index;
        const NodeT* node;
        uint8_t current_depth;
    };
    using node_create_fn = node_creation_status (*)(NodeCreationContext& ctx);
    using homogeneous_fn = bool (*)(HomogeneousContext& ctx);

    static constexpr offset_t invalid_offset = std::numeric_limits<offset_t>::max();
    /*
     * @brief constructs a tree using a tree_create_fn
     *
     *
     * @param create_fn - children create decision function
     * @param homogeneous_fn - optimization, should return true, if all its children have same data
     * @param branching_factor_<axis> - branching factors per each axis, product of these axes must
     * be equal to the branching factor of the tree node
     * @param max_depth - subdivision level of the tree, depth counts edges (0 == root only)
     */
    template <typename NodeCreateFn, typename HomogeneousFn>
    tree_return_status create(NodeCreateFn create_fn, HomogeneousFn homogeneous_fn,
                              uint8_t max_depth)
    {
        constexpr size_t morton_length = std::bit_width(NodeT::max_children - 1);

        offset_t read_idx = 0;
        offset_t write_idx = 1;
        uint8_t current_depth = 0;

        m_nodes.resize(0);
        m_morton_codes.resize(0);

        NodeT root_node;
        root_node.clear_bitmask();
        root_node.firstchild = invalid_offset;
        root_node.parent = invalid_offset;
        NodeCreationContext root_ctx = {1, nullptr, &root_node.data, current_depth};
        node_creation_status node_status = create_fn(root_ctx);

        if (node_status == node_creation_status_stop)
            return tree_return_status_not_created;

        m_nodes.push_back(root_node);
        m_morton_codes.push_back(1);

        while (current_depth < max_depth)
        {
            while (read_idx < write_idx)
            {
                if (!ensure_has_node(read_idx))
                    return tree_return_status_error;

                offset_t parent_index = read_idx;

                uint64_t morton = m_morton_codes[read_idx];

                HomogeneousContext homogeneous_ctx = {morton, &m_nodes[parent_index],
                                                      current_depth};
                bool is_homogeneous = homogeneous_fn && homogeneous_fn(homogeneous_ctx);

                NodeT children[NodeT::max_children];

                if (!is_homogeneous)
                {
                    offset_t prev_idx = invalid_offset;
                    for (size_t i = 0; i < NodeT::max_children; i++)
                    {
                        NodeT& new_node = children[i];

                        uint64_t new_morton = (morton << morton_length) | i;
                        NodeCreationContext ctx = {new_morton, &m_nodes[parent_index],
                                                   &new_node.data, current_depth + 1};
                        node_status = create_fn(ctx);

                        if (node_status == node_creation_status_create)
                        {
                            m_nodes[parent_index].set_bitmask(i);
                            if (is_homogeneous && prev_idx != invalid_offset)
                            {
                                if (new_node.data != children[prev_idx].data)
                                {
                                    is_homogeneous = false;
                                }
                            }
                            prev_idx = i;
                        }
                    }
                    if (is_homogeneous && prev_idx != invalid_offset) // TODO: depends on usecase
                    {
                        m_nodes[parent_index].data = children[prev_idx].data;
                    }
                }

                if (!is_homogeneous)
                {
                    if (m_nodes[parent_index].firstchild == invalid_offset)
                        m_nodes[parent_index].firstchild = m_nodes.size();

                    for (size_t i = m_nodes[parent_index].find_lsb(0);
                         i < m_nodes[parent_index].childmask.find_lsb_none;
                         i = m_nodes[parent_index].find_lsb(++i))
                    {
                        NodeT& new_node = children[i];
                        uint64_t new_morton = (morton << morton_length) | i;

                        new_node.clear_bitmask();
                        new_node.firstchild = invalid_offset;
                        new_node.parent = parent_index;
                        if (true || new_node.data)
                        {
                            m_nodes.push_back(new_node);
                            m_morton_codes.push_back(new_morton);
                        }
                        else
                        {
                            m_nodes[parent_index].reset_bitmask(i);
                        }
                    }
                }
                else
                    m_nodes[parent_index].clear_bitmask();

                read_idx++;
            }

            write_idx = m_nodes.size();
            current_depth++;
        }
        return tree_return_status_created;
    }

    template <typename NodeCreateFn, typename HomogeneousFn>
    tree_return_status modify(NodeCreateFn modify_fn, HomogeneousFn homogeneous_fn,
                              uint8_t max_depth)
    {
        constexpr size_t morton_length = std::bit_width(NodeT::max_children - 1);

        std::queue<size_t> queue;
        queue.push(0);

        while (!queue.empty())
        {
            size_t index = queue.front();
            queue.pop();
        }
    }

    size_t node_count() { return m_nodes.size(); }

    NodeT* node_data() { return m_nodes.data(); }

    uint64_t* node_mortons_data() { return m_morton_codes.data(); }

private:
    inline bool ensure_has_node(size_t index)
    {
        if (index >= m_nodes.size())
        {
            m_nodes.resize(std::max(m_nodes.size() + m_nodes.size() / 2, index + 1));
            m_morton_codes.resize(std::max(m_nodes.size() + m_nodes.size() / 2, index + 1));
        }
        return true;
    }

    struct Range
    {
        size_t offset;
        size_t size;
    };

    void free_children(size_t parent_idx)
    {
        std::deque<Range> queue;
        {
            Range range;
            range.offset = m_nodes[parent_idx].firstchild;
            range.size = m_nodes[parent_idx].child_count();
            queue.push_back(range);
        }

        while (!queue.empty())
        {
            Range range = queue.front();
            queue.pop_front();

            size_t end = range.offset + range.size;
            for (size_t i = range.offset; i < end; i++)
            {
                NodeT& node = m_nodes[i];
                size_t child_count = node.child_count();
                if (child_count > 0)
                    queue.emplace_back(node.firstchild, child_count);
            }

            m_free_list.insert_free(range.offset, range.size);
        }
    }

    svo::FreeList<offset_t, NodeT::max_children> m_free_list;
    std::vector<NodeT> m_nodes;
    std::vector<uint64_t> m_morton_codes;
};

} // namespace svo
