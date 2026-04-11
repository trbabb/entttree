#pragma once

#include <geomc/shape/Transformed.h>

#include <entttree/transform_hierarchy.h>

namespace entttree {


/**
 * @brief A system for maintaining hierarchical bounding boxes.
 *
 * Computed bounds for each entity are the union of its intrinsic bounds and the
 * computed bounds of its children (transformed into parent space). Dirty tracking
 * defers recomputation until the bounds are actually queried.
 *
 * Listens to hierarchy and transform signals for automatic dirty propagation.
 */
template <typename HTag, typename T, size_t N>
struct BoundsSystem {

    using xfn    = AffineTransform<T,N>;
    using vecn   = Vec<T,N>;
    using rangen = Rect<T,N>;
    using rayn   = Ray<T,N>;
    using IB     = IntrinsicBounds<HTag,T,N>;

    /****************************
     * Typed signals
     ****************************/

    Signal<entt::entity, rangen>           on_bounds_set;
    Signal<entt::entity, rangen>           on_bounds_removed;
    Signal<entt::entity, rangen, rangen>   on_bounds_changed;

    /****************************
     * Construction
     ****************************/

    BoundsSystem(
            entt::registry& reg,
            TransformSystem<HTag,T,N>& transforms):
        _reg(reg),
        _transforms(transforms)
    {
        // listen for hierarchy changes to dirty bounds
        _conn_added = _transforms.hierarchy().on_added
            .template connect<&BoundsSystem::_on_child_added>(*this);
        _conn_removed = _transforms.hierarchy().on_removed
            .template connect<&BoundsSystem::_on_child_removed>(*this);
        _conn_changed = _transforms.hierarchy().on_changed
            .template connect<&BoundsSystem::_on_reparent>(*this);

        // listen for transform changes to dirty bounds
        _conn_xf = _transforms.on_transform_changed
            .template connect<&BoundsSystem::_on_transform_changed>(*this);
    }

    ~BoundsSystem() {
        _transforms.hierarchy().on_added.disconnect(_conn_added);
        _transforms.hierarchy().on_removed.disconnect(_conn_removed);
        _transforms.hierarchy().on_changed.disconnect(_conn_changed);
        _transforms.on_transform_changed.disconnect(_conn_xf);
    }

    BoundsSystem(const BoundsSystem&) = delete;
    BoundsSystem& operator=(const BoundsSystem&) = delete;

    TransformSystem<HTag,T,N>& transform_system() { return _transforms; }
    HierarchySystem<HTag>& hierarchy() { return _transforms.hierarchy(); }

    /****************************
     * Mutation API
     ****************************/

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


    std::optional<rangen> get_intrinsic_bounds(entt::entity eid) const {
        auto* ib = _reg.try_get<IB>(eid);
        if (ib) return ib->bounds;
        return std::nullopt;
    }


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

    auto traverse(entt::entity root, SiblingTraversalOrder order) {
        return augment_with_bounds(
            _transforms.traverse(root, order)
        );
    }


    template <TransformedTraversal<T,N> Traversal>
    auto augment_with_bounds(Traversal&& t) {
        using Node = typename Traversal::Node::InnerNode;
        return entttree::transform(
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


    template <BoundedTraversal<T,N> Traversal>
    auto traverse_under_point(Traversal&& t, vecn p) {
        using InnerNode = typename Traversal::Node::InnerNode;
        return entttree::filter(
            entttree::transform(
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


    Generator<PointSearchNode<NodeEntry,T,N>> search_under_point(
            entt::entity root,
            SiblingTraversalOrder sibling_order,
            RecursionOrder recursion_order,
            vecn p)
    {
        auto g = entttree::traverse_dfs(
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


    template <BoundedTraversal<T,N> Traversal>
    auto traverse_along_ray(Traversal&& t, rayn ray) {
        using InnerNode = typename Traversal::Node::InnerNode;
        return entttree::filter(
            entttree::transform(
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


    Generator<RaySearchNode<NodeEntry,T,N>> search_along_ray(
            entt::entity root,
            SiblingTraversalOrder sibling_order,
            RecursionOrder recursion_order,
            rayn ray)
    {
        auto g = entttree::traverse_dfs(
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
    TransformSystem<HTag,T,N>& _transforms;

    DenseSet<entt::entity> _dirty;
    DenseMap<entt::entity, rangen> _computed;

    // signal connection IDs for cleanup
    size_t _conn_added;
    size_t _conn_removed;
    size_t _conn_changed;
    size_t _conn_xf;


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

        for (auto g = _transforms.hierarchy().children(eid, SiblingTraversalOrder::Forward); g; ++g) {
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

    void _on_transform_changed(entt::entity eid, xfn old_xf, xfn new_xf) {
        // transform changed => parent's computed bounds are stale
        entt::entity parent = _transforms.hierarchy().parent_of(eid);
        if (parent != entt::null) {
            _dirty_ancestors(parent);
        }
    }

};


template <typename HTag>
using BoundsSystem2d = BoundsSystem<HTag, double, 2>;

template <typename HTag>
using BoundsSystem3d = BoundsSystem<HTag, double, 3>;


} // namespace entttree
