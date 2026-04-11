#pragma once

#include <entttree/hierarchy.h>

namespace entttree {


/**
 * @brief A system for maintaining local transforms layered on a hierarchy.
 *
 * Objects without an explicit transform are assumed to have the identity.
 * Emits typed signals for transform add/change/remove.
 */
template <typename HTag, typename T, size_t N>
struct TransformSystem {

    using xfn = AffineTransform<T,N>;
    using LT  = LocalTransform<HTag,T,N>;

    /****************************
     * Typed signals
     ****************************/

    Signal<entt::entity, xfn>            on_transform_set;
    Signal<entt::entity, xfn>            on_transform_removed;
    Signal<entt::entity, xfn, xfn>       on_transform_changed;

    /****************************
     * Construction
     ****************************/

    TransformSystem(entt::registry& reg, HierarchySystem<HTag>& hierarchy):
        _reg(reg), _hierarchy(hierarchy) {}

    TransformSystem(const TransformSystem&) = delete;
    TransformSystem& operator=(const TransformSystem&) = delete;

    HierarchySystem<HTag>& hierarchy() { return _hierarchy; }
    const HierarchySystem<HTag>& hierarchy() const { return _hierarchy; }

    /****************************
     * Mutation API
     ****************************/

    void set_transform(entt::entity eid, xfn xf) {
        auto* old = _reg.try_get<LT>(eid);
        if (old) {
            xfn old_xf = old->child_to_parent;
            old->child_to_parent = xf;
            if (old_xf != xf) {
                on_transform_changed.publish(eid, old_xf, xf);
            }
        } else {
            _reg.emplace<LT>(eid, xf);
            on_transform_set.publish(eid, xf);
        }
    }

    std::optional<xfn> get_transform(entt::entity eid) const {
        auto* lt = _reg.try_get<LT>(eid);
        if (lt) return lt->child_to_parent;
        return std::nullopt;
    }

    std::optional<LT> remove_transform(entt::entity eid) {
        auto* lt = _reg.try_get<LT>(eid);
        if (not lt) return std::nullopt;
        LT old = *lt;
        _reg.erase<LT>(eid);
        on_transform_removed.publish(eid, old.child_to_parent);
        return old;
    }

    /****************************
     * Queries
     ****************************/

    xfn object_to_world(entt::entity node) const {
        xfn xf;
        entt::entity cur = node;
        while (cur != entt::null) {
            auto* lt = _reg.try_get<LT>(cur);
            if (lt) {
                xf = lt->child_to_parent * xf;
            }
            cur = _hierarchy.parent_of(cur);
        }
        return xf;
    }


    xfn xf_between(entt::entity from_node, entt::entity to_node) const {
        TreePath p0 = _hierarchy.path(from_node);
        TreePath p1 = _hierarchy.path(to_node);

        int common_root_depth = -1;
        size_t n = std::min(p0.size(), p1.size());
        for (size_t i = 0; i < n; ++i) {
            if (p0[i] == p1[i]) {
                common_root_depth = (int) i;
            } else {
                break;
            }
        }

        xfn xf_to_p0;
        for (int i = common_root_depth + 1; i < (int) p0.size(); ++i) {
            auto* lt = _reg.try_get<LT>(p0[i]);
            if (lt) xf_to_p0 *= lt->child_to_parent;
        }

        xfn xf_to_p1;
        for (int i = common_root_depth + 1; i < (int) p1.size(); ++i) {
            auto* lt = _reg.try_get<LT>(p1[i]);
            if (lt) xf_to_p1 *= lt->child_to_parent;
        }

        return xf_to_p0 / xf_to_p1;
    }


    /****************************
     * Traversal
     ****************************/

    auto traverse(entt::entity root, SiblingTraversalOrder order) const {
        return augment_with_transforms(
            _hierarchy.traverse(root, order),
            [](const NodeEntry& n) { return n.node_id; }
        );
    }


    template <AnyTraversalConcept Traversal, typename GetId>
    auto augment_with_transforms(Traversal&& t, GetId&& get_id) const {
        using Node = typename Traversal::Node;
        return make_traversal(
            t.root
                ? std::make_optional(
                    TransformedNode<Node,T,N> {
                        *std::forward<Traversal>(t).root,
                        {},
                    }
                )
                : std::nullopt,
            [
                this,
                successors = std::forward<Traversal>(t).successors,
                get_id
            ] (TransformedNode<Node,T,N>& parent) -> Generator<TransformedNode<Node,T,N>> {
                for (auto g = successors(parent.node); g; ++g) {
                    auto&& n = *g;
                    entt::entity child_id = get_id(n);
                    auto* lt = _reg.try_get<LT>(child_id);
                    co_yield TransformedNode<Node,T,N> {
                        std::move(n),
                        lt ? (parent.node_to_root * lt->child_to_parent)
                           : parent.node_to_root
                    };
                }
            }
        );
    }


private:

    entt::registry& _reg;
    HierarchySystem<HTag>& _hierarchy;

};


template <typename HTag>
using TransformSystem2d = TransformSystem<HTag, double, 2>;

template <typename HTag>
using TransformSystem3d = TransformSystem<HTag, double, 3>;


} // namespace entttree
