/**
 * @file bounding_hierarchy.h
 * @brief Hierarchical bounding volume system with lazy recomputation.
 */

#pragma once

#include <type_traits>

#include <geomc/shape/Transformed.h>

#include <entttree/transform_hierarchy.h>

namespace entttree {


/**
 * @brief A system for maintaining hierarchical bounding boxes.
 *
 * It produces a computed bound for each entity, which is the union of its
 * intrinsic bound and the computed bounds of its children (transformed into
 * parent space). Dirty tracking defers recomputation until the bounds are
 * actually queried, making bulk hierarchy or transform changes inexpensive.
 *
 * This system connects to a TransformSystem (and through it, a HierarchySystem)
 * via signals. When parent-child relationships change or transforms are
 * modified, the affected bounds are automatically marked dirty up to the root.
 *
 * The BoundsSystem does not own the TransformSystem; it holds a reference
 * to it. Multiple BoundsSystem instances may share the same TransformSystem
 * if needed.
 *
 * @tparam HTag Hierarchy tag type.
 * @tparam T    Scalar type (e.g. `double`).
 * @tparam N    Spatial dimension (e.g. 2 or 3).
 * @tparam BTag Optional bounds-layer tag. Defaults to HTag.
 * @tparam XTag Transform-layer tag to observe. Defaults to HTag.
 */
template <
    typename HTag,
    typename T=double,
    size_t N=2,
    typename BTag=HTag,
    typename XTag=HTag>
struct BoundsSystem {

    using xfn    = AffineTransform<T,N>;
    using vecn   = Vec<T,N>;
    using rangen = Rect<T,N>;
    using rayn   = Ray<T,N>;
    using IB     = IntrinsicBounds<HTag,T,N,BTag>;

    /****************************
     * Typed signals
     ****************************/

    /// Emitted when intrinsic bounds are first set on an entity. Args: (entity, new_bounds).
    entt::sigh<void(entt::entity, rangen)>         on_bounds_set;
    /// Emitted when intrinsic bounds are removed from an entity. Args: (entity, old_bounds).
    entt::sigh<void(entt::entity, rangen)>         on_bounds_removed;
    /// Emitted when an existing intrinsic bounds value changes. Args: (entity, old_bounds, new_bounds).
    entt::sigh<void(entt::entity, rangen, rangen)> on_bounds_changed;

    /****************************
     * Construction
     ****************************/

    BoundsSystem(
            entt::registry& reg,
            TransformSystem<HTag,T,N,XTag>& transforms):
        _reg(reg),
        _transforms(transforms)
    {
        // listen for hierarchy changes to dirty bounds
        _conn_added = entt::sink{_transforms.hierarchy().on_added}
            .template connect<&BoundsSystem::_on_child_added>(*this);
        _conn_removed = entt::sink{_transforms.hierarchy().on_removed}
            .template connect<&BoundsSystem::_on_child_removed>(*this);
        _conn_changed = entt::sink{_transforms.hierarchy().on_changed}
            .template connect<&BoundsSystem::_on_reparent>(*this);

        // listen for transform edits to dirty bounds
        _conn_xf_set = entt::sink{_transforms.on_transform_set}
            .template connect<&BoundsSystem::_on_transform_set>(*this);
        _conn_xf_removed = entt::sink{_transforms.on_transform_removed}
            .template connect<&BoundsSystem::_on_transform_removed>(*this);
        _conn_xf_changed = entt::sink{_transforms.on_transform_changed}
            .template connect<&BoundsSystem::_on_transform_changed>(*this);
    }

    BoundsSystem(const BoundsSystem&) = delete;
    BoundsSystem& operator=(const BoundsSystem&) = delete;

    TransformSystem<HTag,T,N,XTag>& transform_system() { return _transforms; }
    HierarchySystem<HTag>& hierarchy() { return _transforms.hierarchy(); }

    /****************************
     * Mutation API
     ****************************/

    /**
     * @brief Set the intrinsic (local) bounding box for an entity.
     * @return The previous intrinsic bounds, or `std::nullopt` if the entity had none.
     */
    std::optional<rangen> set_intrinsic_bounds(entt::entity eid, rangen bounds) {
        auto* old = _reg.try_get<IB>(eid);
        std::optional<rangen> old_val;
        if (old) {
            old_val = old->bounds;
            if (old->bounds != bounds) {
                old->bounds = bounds;
                _dirty_ancestors(eid);
                on_bounds_changed.publish(eid, *old_val, bounds);
            }
        } else {
            _reg.emplace<IB>(eid, bounds);
            _dirty_ancestors(eid);
            on_bounds_set.publish(eid, bounds);
        }
        return old_val;
    }


    /// Get the intrinsic bounds in local coordinates, or `std::nullopt` if not set.
    std::optional<rangen> get_intrinsic_bounds(entt::entity eid) const {
        auto* ib = _reg.try_get<IB>(eid);
        if (ib) return ib->bounds;
        return std::nullopt;
    }


    /// Remove the intrinsic bounds for an entity. Returns the old bounds if they existed.
    std::optional<rangen> remove_intrinsic_bounds(entt::entity eid) {
        auto* ib = _reg.try_get<IB>(eid);
        if (not ib) return std::nullopt;
        rangen old = ib->bounds;
        _reg.erase<IB>(eid);

        if (_transforms.hierarchy().parent_of(eid) == entt::null
            and _transforms.hierarchy().child_count(eid) == 0)
        {
            _dirty.erase(eid);
            _computed.erase(eid);
        } else {
            _dirty_ancestors(eid);
        }
        on_bounds_removed.publish(eid, old);
        return old;
    }


    /**
     * @brief Get the computed bounds for an entity in local coordinates.
     *
     * Computed bounds are the union of the entity's intrinsic bounds and the
     * computed bounds of all its children (transformed into parent space).
     * If the bounds are dirty, they are lazily recomputed before returning.
     *
     * @return The computed bounds, or `std::nullopt` if the entity has no
     *         intrinsic bounds and no children with bounds.
     */
    std::optional<rangen> get_computed_bounds(entt::entity eid) {
        if (_dirty.contains(eid)) {
            return _recompute(eid);
        }
        auto it = _computed.find(eid);
        if (it != _computed.end()) return it->second;
        return std::nullopt;
    }


    /****************************
     * Traversal
     ****************************/

    /// Create a traversal which yields `BoundedNode<NodeEntry,T,N>`.
    auto traverse(entt::entity root, SiblingOrder order) {
        return augment_with_bounds(
            _transforms.traverse(root, order)
        );
    }


    /**
     * @brief Convert a traversal of `TransformedNode<Node,T,N>` to a traversal
     * of `BoundedNode<Node,T,N>`.
     *
     * Each node is augmented with its intrinsic and computed bounds.
     * Dirty bounds are recomputed on the fly.
     */
    template <TransformedTraversal<T,N> Traversal>
    auto augment_with_bounds(Traversal&& t) {
        using Node = typename TraversalValue<Traversal>::Node::InnerNode;
        return entttree::walk::map_nodes(
            std::forward<Traversal>(t),
            [this] (TransformedNode<Node,T,N>& n) -> BoundedNode<Node,T,N> {
                entt::entity eid = n.node.node_id;
                std::optional<rangen> computed;
                if (_dirty.contains(eid)) {
                    computed = _recompute(eid);
                } else {
                    auto it = _computed.find(eid);
                    if (it != _computed.end()) computed = it->second;
                }
                return {
                    n,
                    get_intrinsic_bounds(eid),
                    computed
                };
            }
        );
    }


    /**
     * @brief Filter a bounded traversal to only visit nodes whose computed bounds
     * contain a point (or which have descendants that might).
     *
     * Transforms the traversal from `BoundedNode<Node,T,N>` to
     * `PointSearchNode<Node,T,N>`, which includes the query point in each
     * node's local coordinates for convenience. Nodes without computed bounds
     * are traversed unconditionally.
     */
    template <BoundedTraversal<T,N> Traversal>
    auto traverse_under_point(Traversal&& t, vecn p) {
        using InnerNode = typename TraversalValue<Traversal>::Node::InnerNode;
        return entttree::walk::exclude_if(
            entttree::walk::map_nodes(
                std::forward<Traversal>(t),
                [p](BoundedNode<InnerNode,T,N>& n) -> PointSearchNode<InnerNode,T,N> {
                    return {n, p / n.node_to_root};
                }
            ),
            [](const PointSearchNode<InnerNode,T,N>& n) {
                if (not n.computed_bounds) return true;
                return n.computed_bounds->contains(n.local_point);
            }
        );
    }


    /**
     * @brief Yield all nodes whose intrinsic bounds contain a point.
     *
     * This performs a depth-first traversal, pruning subtrees whose computed
     * bounds do not contain the point, and then post-filters to only yield
     * nodes whose *intrinsic* bounds contain the local point.
     *
     * @param root           Root of the subtree to search.
     * @param sibling_order  Order in which siblings are visited.
     * @param recursion_order Pre-order or post-order visitation.
     * @param p              The query point in root-space coordinates.
     */
    Generator<PointSearchNode<NodeEntry,T,N>> search_under_point(
            entt::entity root,
            SiblingOrder sibling_order,
            DfsOrder recursion_order,
            vecn p)
    {
        auto g = entttree::walk::dfs(
            traverse_under_point(traverse(root, sibling_order), p),
            recursion_order
        );
        for (; g; ++g) {
            auto&& n = *g;
            if (n.intrinsic_bounds and n.intrinsic_bounds->contains(n.local_point)) {
                co_yield n;
            }
        }
    }


    /**
     * @brief Filter a bounded traversal to only visit nodes whose computed bounds
     * intersect a ray (or which have descendants that might).
     *
     * Transforms the traversal from `BoundedNode<Node,T,N>` to
     * `RaySearchNode<Node,T,N>`, which includes the local ray and the
     * parameter interval of the intersection. Nodes without computed bounds
     * are traversed unconditionally.
     */
    template <BoundedTraversal<T,N> Traversal>
    auto traverse_along_ray(Traversal&& t, rayn ray) {
        using InnerNode = typename TraversalValue<Traversal>::Node::InnerNode;
        return entttree::walk::exclude_if(
            entttree::walk::map_nodes(
                std::forward<Traversal>(t),
                [ray](BoundedNode<InnerNode,T,N>& n) -> RaySearchNode<InnerNode,T,N> {
                    rayn local_ray = ray / n.node_to_root;
                    if (not n.computed_bounds) {
                        return {n, local_ray, Rect<T,1>::full};
                    }
                    rangen box = *n.computed_bounds;
                    auto interval = box.intersect(local_ray);
                    return {n, local_ray, interval};
                }
            ),
            [](const RaySearchNode<InnerNode,T,N>& n) {
                return not n.interval.is_empty();
            }
        );
    }


    /**
     * @brief Yield all nodes whose computed bounds intersect a ray.
     *
     * This performs a depth-first traversal, pruning subtrees whose computed
     * bounds do not intersect the ray.
     *
     * @param root           Root of the subtree to search.
     * @param sibling_order  Order in which siblings are visited.
     * @param recursion_order Pre-order or post-order visitation.
     * @param ray            The query ray in root-space coordinates.
     */
    Generator<RaySearchNode<NodeEntry,T,N>> search_along_ray(
            entt::entity root,
            SiblingOrder sibling_order,
            DfsOrder recursion_order,
            rayn ray)
    {
        auto g = entttree::walk::dfs(
            traverse_along_ray(traverse(root, sibling_order), ray),
            recursion_order
        );
        for (; g; ++g) {
            if (g->interval.is_empty()) continue;
            co_yield *g;
        }
    }


private:

    entt::registry& _reg;
    TransformSystem<HTag,T,N,XTag>& _transforms;

    DenseSet<entt::entity> _dirty;
    DenseMap<entt::entity, rangen> _computed;

    // scoped signal connections (auto-disconnect on destruction)
    entt::scoped_connection _conn_added;
    entt::scoped_connection _conn_removed;
    entt::scoped_connection _conn_changed;
    entt::scoped_connection _conn_xf_set;
    entt::scoped_connection _conn_xf_removed;
    entt::scoped_connection _conn_xf_changed;


    void _dirty_ancestors(entt::entity eid) {
        // dirty this node and all ancestors up to the root.
        // ancestors() requires a ParentConnection, but root nodes don't have one,
        // so we walk manually via parent_of().
        entt::entity cur = eid;
        while (cur != entt::null) {
            auto [_, was_inserted] = _dirty.insert(cur);
            if (not was_inserted) break;  // already dirty above here
            cur = _transforms.hierarchy().parent_of(cur);
        }
    }


    std::optional<rangen> _recompute(entt::entity eid) {
        rangen b = rangen::empty;

        auto* ib = _reg.try_get<IB>(eid);
        if (ib) b = ib->bounds;

        for (auto g = _transforms.hierarchy().children(eid, SiblingOrder::Forward); g; ++g) {
            entt::entity child = g->node_id;
            rangen child_bound = rangen::empty;

            if (_dirty.contains(child)) {
                auto cb = _recompute(child);
                if (cb) child_bound = *cb;
                _dirty.erase(child);
            } else {
                auto it = _computed.find(child);
                if (it != _computed.end()) child_bound = it->second;
            }

            if (not child_bound.is_empty()) {
                auto xf = _transforms.get_transform(child);
                if (xf) {
                    child_bound = ((*xf) * child_bound).bounds();
                }
                b |= child_bound;
            }
        }

        if (not b.is_empty()) {
            _computed.insert_or_assign(eid, b);
        } else {
            _computed.erase(eid);
        }
        _dirty.erase(eid);
        return b.is_empty() ? std::nullopt : std::optional{b};
    }


    /****************************
     * Signal handlers
     ****************************/

    void _on_child_added(entt::entity child, ParentConnection<HTag> pc) {
        _dirty_ancestors(pc.parent);
    }

    void _on_child_removed(entt::entity child, ParentConnection<HTag> old_pc) {
        _dirty_ancestors(old_pc.parent);
        if (_transforms.hierarchy().child_count(child) == 0
            and not get_intrinsic_bounds(child))
        {
            _dirty.erase(child);
        }
    }

    void _on_reparent(
            entt::entity child,
            ParentConnection<HTag> old_val,
            ParentConnection<HTag> new_val)
    {
        if (old_val.parent != new_val.parent) {
            _dirty_ancestors(old_val.parent);
            _dirty_ancestors(child);
        }
    }

    void _on_transform_set(entt::entity eid, xfn new_xf) {
        _dirty_parent_after_transform_edit(eid);
    }

    void _on_transform_removed(entt::entity eid, xfn old_xf) {
        _dirty_parent_after_transform_edit(eid);
    }

    void _on_transform_changed(entt::entity eid, xfn old_xf, xfn new_xf) {
        _dirty_parent_after_transform_edit(eid);
    }

    void _dirty_parent_after_transform_edit(entt::entity eid) {
        // child-to-parent transform edit => parent's computed bounds are stale
        entt::entity parent = _transforms.hierarchy().parent_of(eid);
        if (parent != entt::null) {
            _dirty_ancestors(parent);
        }
    }

};


/**
 * @brief Construct a BoundsSystem with deduced types from a TransformSystem.
 *
 * Easy path:
 * `auto bs = add_bounds(reg, transforms);`
 *
 * Layered path:
 * `auto bs = add_bounds<RenderBounds>(reg, transforms);`
 *
 * @tparam BTag Bounds-layer tag. Defaults to the hierarchy tag when omitted.
 * @tparam HTag Hierarchy tag (deduced from `transforms`).
 * @tparam T    Scalar type (deduced from `transforms`).
 * @tparam N    Spatial dimension (deduced from `transforms`).
 * @tparam XTag Transform-layer tag observed by this bounds system (deduced).
 */
template <typename BTag=void, typename HTag, typename T, size_t N, typename XTag>
auto add_bounds(
        entt::registry& reg,
        TransformSystem<HTag,T,N,XTag>& transforms)
{
    using LayerTag = std::conditional_t<std::is_void_v<BTag>, HTag, BTag>;
    return BoundsSystem<HTag,T,N,LayerTag,XTag>(reg, transforms);
}


template <typename HTag, typename BTag=HTag, typename XTag=HTag>
using BoundsSystem2d = BoundsSystem<HTag, double, 2, BTag, XTag>;

template <typename HTag, typename BTag=HTag, typename XTag=HTag>
using BoundsSystem3d = BoundsSystem<HTag, double, 3, BTag, XTag>;


} // namespace entttree
