/**
 * @file defs.h
 * @brief Common type aliases, enumerations, and utility functions used throughout entttree.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>

#include <ankerl/unordered_dense.h>

#include <geomc/linalg/LinalgTypes.h>
#include <geomc/shape/ShapeTypes.h>
#include <geomc/Hash.h>
#include <geomc/SmallStorage.h>

#include <entt/entt.hpp>

using namespace geom;

namespace entttree {

/// High-performance hash map backed by ankerl::unordered_dense.
template <typename K, typename V, typename Hasher=std::hash<K>, typename KeyEqual=std::equal_to<K>>
using DenseMap = ankerl::unordered_dense::map<K,V,Hasher,KeyEqual>;

/// High-performance hash set backed by ankerl::unordered_dense.
template <typename T, typename Hasher=std::hash<T>, typename KeyEqual=std::equal_to<T>>
using DenseSet = ankerl::unordered_dense::set<T,Hasher,KeyEqual>;

/// Controls the visit order for depth-first traversal.
enum struct DfsOrder {
    /// Visit a node before its descendants (pre-order).
    ShallowFirst,
    /// Visit a node after its descendants (post-order).
    DeepFirst
};

/// Controls the iteration order of siblings within a parent.
enum struct SiblingOrder {
    /// Iterate siblings in ascending position order.
    Forward,
    /// Iterate siblings in descending position order.
    Backward
};


/**
 * @brief Look up a key in a DenseMap, returning an optional value.
 * @return The value associated with `k`, or `std::nullopt` if not found.
 */
template <typename K, typename V>
std::optional<V> get_or(const DenseMap<K,V>& m, const K& k) {
    auto i = m.find(k);
    if (i == m.end()) return std::nullopt;
    return i->second;
}

/**
 * @brief Look up a key in a DenseMap, returning a default if not found.
 * @return The value associated with `k`, or `v` if not found.
 */
template <typename K, typename V>
V get_or(const DenseMap<K,V>& m, const K& k, const V& v) {
    auto i = m.find(k);
    return i == m.end() ? v : i->second;
}

/**
 * @brief Insert or replace a value in a DenseMap.
 * @return The previous value associated with `k`, or `std::nullopt` if the key was new.
 */
template <typename K, typename V, typename Arg>
std::optional<V> exchange(DenseMap<K,V>& m, const K& k, Arg&& v) {
    auto [item, inserted] = m.try_emplace(k, v);
    if (not inserted) {
        V old_value = std::move(item->second);
        item->second = V {std::forward<Arg>(v)};
        return old_value;
    }
    return std::nullopt;
}

/**
 * @brief Remove a key from a DenseMap.
 * @return The removed value, or `std::nullopt` if the key was not present.
 */
template <typename K, typename V>
std::optional<V> remove_item(DenseMap<K,V>& m, const K& k) {
    auto it = m.find(k);
    if (it != m.end()) {
        V old_value = std::move(it->second);
        m.erase(it);
        return old_value;
    }
    return std::nullopt;
}

} // namespace entttree
