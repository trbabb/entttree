#pragma once

#include <algorithm>
#include <cassert>

#include <entttree/hierarchy_types.h>
#include <entttree/signal.h>
#include <entttree/traverse.h>

namespace entttree {


/**
 * @brief A system for maintaining parent-child relationships on entities.
 *
 * Mutations go through this system's API, which maintains a sorted children
 * cache and emits typed signals. The source of truth is the
 * `ParentConnection<HTag>` component in the registry.
 *
 * Multiple independent hierarchies can coexist on the same entity by using
 * different tag types.
 */
template <typename HTag>
struct HierarchySystem {

    using PC = ParentConnection<HTag>;

    /****************************
     * Typed signals
     ****************************/

    Signal<entt::entity, PC>     on_added;
    Signal<entt::entity, PC>     on_removed;
    Signal<entt::entity, PC, PC> on_changed;

    /****************************
     * Construction
     ****************************/

    explicit HierarchySystem(entt::registry& reg): _reg(reg) {}

    // non-copyable, non-movable (signal connections hold `this`)
    HierarchySystem(const HierarchySystem&) = delete;
    HierarchySystem& operator=(const HierarchySystem&) = delete;

    /****************************
     * Mutation API
     ****************************/

    /**
     * @brief Set the parent of a child entity.
     *
     * If the child already has a parent, it is reparented. If no position is
     * given, the child is placed at the end of its new parent's children.
     *
     * Returns the actual position used (may be deduplicated).
     */
    Position set_parent(
            entt::entity child,
            entt::entity parent,
            std::optional<Position> position = std::nullopt)
    {
        bool maybe_dupe = true;

        // ensure the parent has a child list
        auto it = _children.find(parent);
        if (it == _children.end()) {
            position = position.value_or(Position{});
            maybe_dupe = false;
            std::tie(it, std::ignore) = _children.emplace(
                parent,
                ChildList{}
            );
        }
        ChildList& children = it->second;
        if (not position and children.size() > 0) {
            position = children.back().position.after();
            maybe_dupe = false;
        }

        // read old state before mutation
        auto* old_pc = _reg.try_get<PC>(child);
        std::optional<PC> old_val;
        if (old_pc) old_val = *old_pc;

        // write the component
        _reg.emplace_or_replace<PC>(child, parent, *position);

        // update children cache
        if (old_val and old_val->parent == parent) {
            // same parent, reorder
            auto src = std::ranges::lower_bound(
                children,
                ChildEntry {.eid = child, .position = old_val->position},
                [](const ChildEntry& a, const ChildEntry& b) { return a < b; }
            );
            auto dst = std::ranges::lower_bound(
                children, *position, std::ranges::less{}, &ChildEntry::position
            );
            if (dst != src) {
                src->position = *position;
                if (maybe_dupe) _dedupe_position(children, src->position, dst);
                if (dst < src) {
                    std::rotate(dst, src, src + 1);
                } else {
                    std::rotate(src, src + 1, std::min(children.end(), dst + 1));
                }
            }
        } else {
            if (old_val) {
                // remove from old parent's child list.
                // this may invalidate `children` ref if the old parent's
                // entry is erased from the DenseMap, so we re-lookup after.
                _remove_from_child_list(old_val->parent, child, old_val->position);
                it = _children.find(parent);
            }
            // insert into new parent's child list
            ChildList& new_children = it->second;
            auto dst_pos = std::ranges::lower_bound(
                new_children, *position, std::ranges::less{}, &ChildEntry::position
            );
            if (maybe_dupe) _dedupe_position(new_children, *position, dst_pos);
            new_children.insert(
                dst_pos,
                ChildEntry {.eid = child, .position = *position}
            );
        }

        // emit signal
        PC new_val {parent, *position};
        if (old_val) {
            if (*old_val != new_val) {
                on_changed.publish(child, *old_val, new_val);
            }
        } else {
            on_added.publish(child, new_val);
        }

        return *position;
    }


    /**
     * @brief Remove a child from its parent.
     *
     * Returns the old ParentConnection if the child was in the hierarchy.
     */
    std::optional<PC> unparent(entt::entity child) {
        auto* old_pc = _reg.try_get<PC>(child);
        if (not old_pc) return std::nullopt;

        PC old_val = *old_pc;
        _reg.erase<PC>(child);

        _remove_from_child_list(old_val.parent, child, old_val.position);

        on_removed.publish(child, old_val);
        return old_val;
    }


    /**
     * @brief Change the position of a child among its siblings.
     *
     * Returns the new position if changed, nullopt if unchanged.
     */
    std::optional<Position> set_child_position(entt::entity child, Position position) {
        auto* pc = _reg.try_get<PC>(child);
        if (not pc) return std::nullopt;
        if (pc->position == position) return std::nullopt;

        PC old_val = *pc;
        auto it = _children.find(pc->parent);
        assert(it != _children.end());
        ChildList& children = it->second;

        auto old_loc = std::ranges::lower_bound(
            children, ChildEntry {.eid = child, .position = old_val.position},
            [](const ChildEntry& a, const ChildEntry& b) { return a < b; }
        );
        auto new_loc = std::ranges::lower_bound(
            children, position, std::ranges::less{}, &ChildEntry::position
        );
        if (new_loc != old_loc) {
            if (new_loc < old_loc) {
                std::rotate(new_loc, old_loc, old_loc + 1);
            } else {
                std::rotate(old_loc, old_loc + 1, std::min(children.end(), new_loc + 1));
            }
        }

        pc->position = position;
        PC new_val = *pc;
        on_changed.publish(child, old_val, new_val);
        return position;
    }


    /**
     * @brief Move a child to the position before a sibling.
     *
     * Returns the new position if changed, nullopt otherwise.
     */
    std::optional<Position> order_child_before(entt::entity child, entt::entity before) {
        if (child == before) return std::nullopt;

        auto* pc = _reg.try_get<PC>(child);
        if (not pc) return std::nullopt;

        auto it = _children.find(pc->parent);
        assert(it != _children.end());
        ChildList& children = it->second;

        auto child_pos = std::ranges::lower_bound(
            children, ChildEntry {.eid = child, .position = pc->position},
            [](const ChildEntry& a, const ChildEntry& b) { return a < b; }
        );
        assert(child_pos != children.end());

        Position new_pos;
        auto* sibling_pc = _reg.try_get<PC>(before);
        if (sibling_pc and sibling_pc->parent == pc->parent) {
            auto before_pos = std::ranges::lower_bound(
                children, ChildEntry {.eid = before, .position = sibling_pc->position},
                [](const ChildEntry& a, const ChildEntry& b) { return a < b; }
            );

            if (child_pos == before_pos - 1) {
                return child_pos->position;
            } else if (before_pos == children.begin()) {
                new_pos = before_pos->position.before();
            } else {
                new_pos = before_pos[-1].position.between(before_pos->position);
            }

            if (child_pos < before_pos) {
                std::rotate(child_pos, child_pos + 1, before_pos);
            } else {
                std::rotate(before_pos, child_pos, child_pos + 1);
            }
        } else {
            // sibling not found or different parent; put at end
            if (child_pos == children.end() - 1) {
                return child_pos->position;
            }
            new_pos = children.back().position.after();
            std::rotate(child_pos, child_pos + 1, children.end());
        }

        PC old_val = *pc;
        child_pos->position = new_pos;
        pc->position = new_pos;

        on_changed.publish(child, old_val, *pc);
        return new_pos;
    }


    /****************************
     * Queries
     ****************************/

    size_t size() const {
        return _reg.template storage<PC>()
            ? _reg.template storage<PC>()->size()
            : 0;
    }

    entt::entity parent_of(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        return pc ? pc->parent : entt::null;
    }

    std::optional<Position> position_of(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        if (pc) return pc->position;
        return std::nullopt;
    }

    std::optional<PC> get_connection(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        if (pc) return *pc;
        return std::nullopt;
    }

    size_t child_count(entt::entity parent) const {
        auto it = _children.find(parent);
        if (it == _children.end()) return 0;
        return it->second.size();
    }

    std::optional<Position> last_child_position(entt::entity parent) const {
        auto it = _children.find(parent);
        if (it == _children.end()) return std::nullopt;
        const ChildList& children = it->second;
        if (children.empty()) return std::nullopt;
        return children.back().position;
    }


    /****************************
     * Traversal generators
     ****************************/

    Generator<NodeEntry> children(
            entt::entity parent,
            SiblingTraversalOrder order) const
    {
        auto it = _children.find(parent);
        if (it == _children.end()) co_return;
        const ChildList& ch = it->second;
        if (order == SiblingTraversalOrder::Forward) {
            for (const auto& c : ch) {
                co_yield NodeEntry {c.eid, parent, c.position};
            }
        } else {
            for (const auto& c : std::ranges::reverse_view(ch)) {
                co_yield NodeEntry {c.eid, parent, c.position};
            }
        }
    }


    Generator<NodeEntry> ancestors(entt::entity node) const {
        entt::entity cur = node;
        while (cur != entt::null) {
            auto* pc = _reg.try_get<PC>(cur);
            if (not pc) co_return;
            co_yield NodeEntry {cur, pc->parent, pc->position};
            cur = pc->parent;
        }
    }


    auto traverse(entt::entity root, SiblingTraversalOrder order) const {
        NodeEntry root_entry {
            root,
            parent_of(root),
            position_of(root).value_or(Position{})
        };
        return make_traversal(
            std::make_optional(root_entry),
            [this, order] (NodeEntry& node) {
                return this->children(node.node_id, order);
            }
        );
    }


    Generator<NodeEntry> traverse_dfs(
            entt::entity root,
            SiblingTraversalOrder sibling_order = SiblingTraversalOrder::Forward,
            RecursionOrder recursion_order = RecursionOrder::ShallowFirst) const
    {
        return entttree::traverse_dfs(
            this->traverse(root, sibling_order),
            recursion_order
        );
    }


    TreePath path(entt::entity node) const {
        TreePath p;
        p.push_back(node);
        auto* pc = _reg.try_get<PC>(node);
        while (pc and pc->parent != entt::null) {
            p.push_back(pc->parent);
            pc = _reg.try_get<PC>(pc->parent);
        }
        std::reverse(p.begin(), p.end());
        return p;
    }


    entt::entity deepest_common_ancestor(
            entt::entity node_a,
            entt::entity node_b) const
    {
        TreePath p0 = path(node_a);
        TreePath p1 = path(node_b);
        size_t n = std::min(p0.size(), p1.size());
        entt::entity common = entt::null;
        for (size_t i = 0; i < n; ++i) {
            if (p0[i] == p1[i]) {
                common = p0[i];
            } else {
                break;
            }
        }
        return common;
    }


    bool is_ancestor_of(entt::entity ancestor, entt::entity descendant) const {
        auto* pc = _reg.try_get<PC>(descendant);
        while (pc and pc->parent != entt::null) {
            if (pc->parent == ancestor) return true;
            pc = _reg.try_get<PC>(pc->parent);
        }
        return false;
    }


private:

    entt::registry& _reg;
    DenseMap<entt::entity, ChildList> _children;

    #ifndef NDEBUG
    bool _publishing = false;
    #endif


    void _remove_from_child_list(
            entt::entity parent,
            entt::entity child,
            const Position& pos)
    {
        auto it = _children.find(parent);
        if (it == _children.end()) return;
        ChildList& ch = it->second;
        auto loc = std::ranges::lower_bound(
            ch,
            ChildEntry {.eid = child, .position = pos},
            [](const ChildEntry& a, const ChildEntry& b) { return a < b; }
        );
        if (loc != ch.end()) ch.erase(loc);
        if (ch.empty()) _children.erase(it);
    }


    bool _dedupe_position(
            const ChildList& v,
            Position& p,
            typename ChildList::iterator next)
    {
        bool at_start = next == v.begin();
        bool at_end   = next == v.end();
        auto prev   = not at_start ? std::prev(next) : next;
        bool duped  = not at_start and p == prev->position;
             duped |= not at_end   and p == next->position;
        if (duped) [[unlikely]] {
            if (not (at_start or at_end)) {
                p = prev->position.between(next->position);
            } else if (not at_start) {
                p = prev->position.after();
            } else {
                p = next->position.before();
            }
        }
        return duped;
    }

};


} // namespace entttree
