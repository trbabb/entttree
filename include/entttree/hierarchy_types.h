#pragma once

#include <geomc/linalg/AffineTransform.h>
#include <geomc/Hash.h>

#include <entttree/defs.h>
#include <entttree/position.h>
#include <entttree/traverse.h>

namespace entttree {


/****************************
 * Source-of-truth component: one per hierarchy tag
 ****************************/

template <typename HTag>
struct ParentConnection {
    entt::entity parent   = entt::null;
    Position     position;

    bool operator==(const ParentConnection& other) const = default;
};


/****************************
 * Optional layered components
 ****************************/

template <typename HTag, typename T, size_t N>
struct LocalTransform {
    AffineTransform<T,N> child_to_parent;

    bool operator==(const LocalTransform& other) const = default;
};


template <typename HTag, typename T, size_t N>
struct IntrinsicBounds {
    Rect<T,N> bounds;

    bool operator==(const IntrinsicBounds& other) const = default;
};


/****************************
 * Query / traversal node types
 ****************************/

struct NodeEntry {
    entt::entity node_id;
    entt::entity parent_id;
    Position     position;
};

struct ChildEntry {
    entt::entity eid;
    Position     position;

    std::strong_ordering operator<=>(const ChildEntry& other) const {
        auto cmp = position <=> other.position;
        if (cmp != std::strong_ordering::equal) return cmp;
        return entt::to_integral(eid) <=> entt::to_integral(other.eid);
    }

    bool operator==(const ChildEntry& other) const = default;
    bool operator<(const ChildEntry& other) const {
        return (*this <=> other) == std::strong_ordering::less;
    }
};

using ChildList = std::vector<ChildEntry>;

struct TreePath : public SmallStorage<entt::entity, 6> {};


template <typename Node, typename T, size_t N>
struct TransformedNode {
    using InnerNode = Node;
    Node                 node;
    AffineTransform<T,N> node_to_root;
};

template <typename Traversal, typename T, size_t N>
concept TransformedTraversal = TraversalConcept<
    Traversal,
    TransformedNode<typename Traversal::Node::InnerNode,T,N>
>;


template <typename Node, typename T, size_t N>
struct BoundedNode : public TransformedNode<Node,T,N> {
    std::optional<Rect<T,N>> intrinsic_bounds;
    std::optional<Rect<T,N>> computed_bounds;
};

template <typename Traversal, typename T, size_t N>
concept BoundedTraversal = TraversalConcept<
    Traversal,
    BoundedNode<typename Traversal::Node::InnerNode,T,N>
>;


template <typename Node, typename T, size_t N>
struct PointSearchNode : public BoundedNode<Node,T,N> {
    Vec<T,N> local_point;
};

template <typename Node, typename T, size_t N>
struct RaySearchNode : public BoundedNode<Node,T,N> {
    Ray<T,N>  local_ray;
    Rect<T,1> interval;
};


} // namespace entttree
