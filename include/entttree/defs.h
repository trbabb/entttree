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

template <typename K, typename V, typename Hasher=std::hash<K>, typename KeyEqual=std::equal_to<K>>
using DenseMap = ankerl::unordered_dense::map<K,V,Hasher,KeyEqual>;

template <typename T, typename Hasher=std::hash<T>, typename KeyEqual=std::equal_to<T>>
using DenseSet = ankerl::unordered_dense::set<T,Hasher,KeyEqual>;

enum struct DfsOrder {
    ShallowFirst,
    DeepFirst
};

enum struct SiblingOrder {
    Forward,
    Backward
};


template <typename K, typename V>
std::optional<V> get_or(const DenseMap<K,V>& m, const K& k) {
    auto i = m.find(k);
    if (i == m.end()) return std::nullopt;
    return i->second;
}

template <typename K, typename V>
V get_or(const DenseMap<K,V>& m, const K& k, const V& v) {
    auto i = m.find(k);
    return i == m.end() ? v : i->second;
}

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
