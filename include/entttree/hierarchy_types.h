/**
 * @file hierarchy_types.h
 * @brief ECS components and node types used by the hierarchy, transform, and bounds systems.
 */

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

/**
 * @brief ECS component storing an entity's parent and sibling position.
 *
 * This is the source of truth for parent-child relationships in a
 * HierarchySystem. It lives on the *child* entity. The Position field
 * determines the relative ordering among siblings.
 *
 * Do not modify this component directly; use HierarchySystem's mutation API
 * so that the internal children cache and signals stay consistent.
 *
 * @tparam HTag Tag type identifying which hierarchy this connection belongs to.
 */
template <typename HTag>
struct ParentConnection {
    entt::entity parent   = entt::null;  ///< Parent entity, or `entt::null` for roots.
    Position     position;               ///< Sibling ordering key.

    bool operator==(const ParentConnection& other) const = default;
};


/****************************
 * Optional layered components
 ****************************/

/**
 * @brief ECS component storing an entity's local (child-to-parent) affine transform.
 *
 * Used by TransformSystem. Entities without this component are treated as
 * having the identity transform.
 *
 * @tparam HTag Tag type identifying the hierarchy.
 * @tparam T    Scalar type (e.g. `double`).
 * @tparam N    Spatial dimension (e.g. 2 or 3).
 * @tparam XTag Optional transform-layer tag. Defaults to HTag.
 */
template <typename HTag, typename T, size_t N, typename XTag=HTag>
struct LocalTransform {
    AffineTransform<T,N> child_to_parent;  ///< Transform from this node's space to its parent's.

    bool operator==(const LocalTransform& other) const = default;
};


/**
 * @brief ECS component storing an entity's own (intrinsic) bounding box.
 *
 * Used by BoundsSystem. The computed bounds of a node are the union of its
 * intrinsic bounds and the computed bounds of all its children (transformed
 * into parent space).
 *
 * @tparam HTag Tag type identifying the hierarchy.
 * @tparam T    Scalar type.
 * @tparam N    Spatial dimension.
 * @tparam BTag Optional bounds-layer tag. Defaults to HTag.
 */
template <typename HTag, typename T, size_t N, typename BTag=HTag>
struct IntrinsicBounds {
    Rect<T,N> bounds;  ///< Axis-aligned bounding box in local coordinates.

    bool operator==(const IntrinsicBounds& other) const = default;
};


/****************************
 * Query / traversal node types
 ****************************/

/**
 * @brief A node handle yielded by hierarchy traversals and queries.
 *
 * Contains the entity, its parent, and its sibling position.
 */
struct NodeEntry {
    entt::entity node_id;    ///< The entity this entry represents.
    entt::entity parent_id;  ///< The parent entity, or `entt::null` for root traversal entries.
    Position     position;   ///< Sibling ordering key within the parent.
};

/**
 * @brief Internal representation of a child in the sorted children cache.
 *
 * Ordered first by Position, then by entity ID to break ties.
 */
struct ChildEntry {
    entt::entity eid;       ///< Child entity.
    Position     position;  ///< Sibling ordering key.

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

/// Sorted list of children for a single parent.
using ChildList = std::vector<ChildEntry>;

/**
 * @brief A path from a root entity down to a descendant.
 *
 * The first element is the root and the last is the target node.
 * Uses small-storage optimization (inline for paths up to 6 deep).
 */
struct TreePath : public SmallStorage<entt::entity, 6> {};


/**
 * @brief A traversal node augmented with a cumulative affine transform.
 *
 * Yielded by TransformSystem::traverse() and augment_with_transforms().
 * The `node_to_root` transform maps from this node's local space to the
 * traversal root's space.
 *
 * @tparam Node The inner node type (typically NodeEntry).
 * @tparam T    Scalar type.
 * @tparam N    Spatial dimension.
 */
template <typename Node, typename T, size_t N>
struct TransformedNode {
    using InnerNode = Node;
    Node                 node;          ///< The underlying node handle.
    AffineTransform<T,N> node_to_root;  ///< Cumulative transform from node space to root space.
};

/// Concept satisfied by traversals whose node type is `TransformedNode<...,T,N>`.
template <typename Traversal, typename T, size_t N>
concept TransformedTraversal = TraversalConcept<
    Traversal,
    TransformedNode<typename std::remove_cvref_t<Traversal>::Node::InnerNode,T,N>
>;


/**
 * @brief A traversal node augmented with transform and bounding information.
 *
 * Yielded by BoundsSystem::traverse() and augment_with_bounds(). Extends
 * TransformedNode with the entity's intrinsic and computed bounds.
 *
 * @tparam Node The inner node type.
 * @tparam T    Scalar type.
 * @tparam N    Spatial dimension.
 */
template <typename Node, typename T, size_t N>
struct BoundedNode : public TransformedNode<Node,T,N> {
    std::optional<Rect<T,N>> intrinsic_bounds;  ///< The entity's own bounds, if set.
    std::optional<Rect<T,N>> computed_bounds;    ///< Union of intrinsic + children bounds, if any.
};

/// Concept satisfied by traversals whose node type is `BoundedNode<...,T,N>`.
template <typename Traversal, typename T, size_t N>
concept BoundedTraversal = TraversalConcept<
    Traversal,
    BoundedNode<typename std::remove_cvref_t<Traversal>::Node::InnerNode,T,N>
>;


/**
 * @brief A bounded node augmented with a point transformed into local coordinates.
 *
 * Yielded by BoundsSystem::traverse_under_point() and search_under_point().
 *
 * @tparam Node The inner node type.
 * @tparam T    Scalar type.
 * @tparam N    Spatial dimension.
 */
template <typename Node, typename T, size_t N>
struct PointSearchNode : public BoundedNode<Node,T,N> {
    Vec<T,N> local_point;  ///< The query point in this node's local coordinate system.
};

/**
 * @brief A bounded node augmented with a ray and hit interval in local coordinates.
 *
 * Yielded by BoundsSystem::traverse_along_ray() and search_along_ray().
 *
 * @tparam Node The inner node type.
 * @tparam T    Scalar type.
 * @tparam N    Spatial dimension.
 */
template <typename Node, typename T, size_t N>
struct RaySearchNode : public BoundedNode<Node,T,N> {
    Ray<T,N>  local_ray;  ///< The query ray in this node's local coordinate system.
    Rect<T,1> interval;   ///< Parameter interval where the ray intersects the computed bounds.
};


} // namespace entttree
