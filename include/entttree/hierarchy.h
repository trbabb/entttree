/**
 * @file hierarchy.h
 * @brief Core hierarchy system managing parent-child relationships over EnTT entities.
 */

#pragma once

#include <algorithm>
#include <cassert>

#include <entttree/hierarchy_types.h>
#include <entttree/traverse.h>

namespace entttree {


/**
 * @brief A system for maintaining parent-child relationships on entities.
 *
 * The source of truth for a parent-child relationship is a
 * ParentConnection<HTag> component which lives on the *child* entity. This
 * component also contains a Position field which determines the relative order
 * of siblings. A sorted cache is maintained which maps parents to their
 * children; this is updated whenever a parent-child connection is changed.
 * For this reason, it is invalid to edit the ParentConnection component
 * outside of this system.
 *
 * Root nodes (those without parents) do not have a ParentConnection component.
 *
 * Multiple independent hierarchies can coexist on the same entity by using
 * different tag types for `HTag`.
 *
 * @tparam HTag A tag type that distinguishes this hierarchy from others on
 *              the same registry (e.g. `struct RenderH {};`).
 */
template <typename HTag>
struct HierarchySystem {

    using PC = ParentConnection<HTag>;

    /****************************
     * Typed signals
     ****************************/

    /// Emitted after a child is added to the hierarchy. Args: (child, new_connection).
    entt::sigh<void(entt::entity, PC)>     on_added;
    /// Emitted after a child is removed from the hierarchy. Args: (child, old_connection).
    entt::sigh<void(entt::entity, PC)>     on_removed;
    /// Emitted after a child's parent or position changes. Args: (child, old_connection, new_connection).
    entt::sigh<void(entt::entity, PC, PC)> on_changed;

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
            src->position = *position;
            if (dst != src) {
                bool duped = maybe_dupe
                    and _dedupe_position(children, src->position, dst);
                if (dst < src) {
                    std::rotate(dst, src, src + 1);
                    src = dst;
                } else {
                    std::rotate(src, src + 1, dst);
                    src = dst - 1;
                }
                if (duped) {
                    _reg.get<PC>(child).position = src->position;
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
                old_loc = new_loc;
            } else {
                std::rotate(old_loc, old_loc + 1, new_loc);
                old_loc = new_loc - 1;
            }
        }
        old_loc->position = position;

        pc->position = position;
        PC new_val = *pc;
        on_changed.publish(child, old_val, new_val);
        return position;
    }


    /**
     * @brief Move a child to the position before a sibling.
     * 
     * The sibling must belong to the same parent; if it doesn't, the child is moved to the end
     * of its parent.
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
                child_pos = before_pos - 1;
            } else {
                std::rotate(before_pos, child_pos, child_pos + 1);
                child_pos = before_pos;
            }
        } else {
            // sibling not found or different parent; put at end
            if (child_pos == children.end() - 1) {
                return child_pos->position;
            }
            new_pos = children.back().position.after();
            std::rotate(child_pos, child_pos + 1, children.end());
            child_pos = children.end() - 1;
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

    /// The number of non-root nodes in the hierarchy (i.e. entities with a parent).
    size_t size() const {
        return _reg.template view<const PC>().size();
    }

    /// Returns the parent of `node`, or `entt::null` if the node is a root or not in the hierarchy.
    entt::entity parent_of(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        return pc ? pc->parent : entt::null;
    }

    /// Returns the sibling position of `node`, or `std::nullopt` if not in the hierarchy.
    std::optional<Position> position_of(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        if (pc) return pc->position;
        return std::nullopt;
    }

    /// Returns the full ParentConnection for `node`, or `std::nullopt` if not in the hierarchy.
    std::optional<PC> get_connection(entt::entity node) const {
        auto* pc = _reg.try_get<PC>(node);
        if (pc) return *pc;
        return std::nullopt;
    }

    /**
     * @brief Returns the number of children of a given node.
     *
     * If the node is not in the hierarchy, the count is implicitly zero.
     */
    size_t child_count(entt::entity parent) const {
        auto it = _children.find(parent);
        if (it == _children.end()) return 0;
        return it->second.size();
    }

    /// Returns the position of the last child, or `std::nullopt` if there are no children.
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

    /**
     * @brief Visit each child of a given node.
     *
     * It is valid to remove (but not add) children during iteration when
     * the sibling order is backward. Otherwise, changes to the hierarchy
     * will invalidate the iterator.
     */
    Generator<NodeEntry> children(
            entt::entity parent,
            SiblingOrder order) const
    {
        auto it = _children.find(parent);
        if (it == _children.end()) co_return;
        const ChildList& ch = it->second;
        if (order == SiblingOrder::Forward) {
            for (const auto& c : ch) {
                co_yield NodeEntry {c.eid, parent, c.position};
            }
        } else {
            for (const auto& c : std::ranges::reverse_view(ch)) {
                co_yield NodeEntry {c.eid, parent, c.position};
            }
        }
    }


    /**
     * @brief Returns a generator which yields the ancestors of a given node.
     *
     * The first element yielded is the node itself (unless it has no
     * ParentConnection), and the last element is the root. Each yielded
     * NodeEntry has `node_id` set to the current ancestor and `parent_id`
     * set to the next ancestor up the hierarchy. When the root is yielded
     * its `parent_id` will be `entt::null`.
     */
    Generator<NodeEntry> ancestors(entt::entity node) const {
        entt::entity cur = node;
        while (cur != entt::null) {
            auto* pc = _reg.try_get<PC>(cur);
            if (not pc) co_return;
            co_yield NodeEntry {cur, pc->parent, pc->position};
            cur = pc->parent;
        }
    }


    /**
     * @brief Returns a Traversal of the hierarchy rooted at `root`.
     *
     * The returned Traversal can be composed with adaptors from the
     * `walk` namespace and then flattened with `walk::dfs()` or `walk::bfs()`.
     */
    auto traverse(entt::entity root, SiblingOrder order) const {
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

    /// Convenience: flatten a depth-first traversal into a generator.
    Generator<NodeEntry> traverse_dfs(
            entt::entity root,
            SiblingOrder sibling_order = SiblingOrder::Forward,
            DfsOrder recursion_order = DfsOrder::ShallowFirst) const
    {
        return entttree::walk::dfs(
            this->traverse(root, sibling_order),
            recursion_order
        );
    }

    /**
     * @brief Returns the path from the root to the given node.
     *
     * The first element of the path is the root, and the last element
     * is the node itself.
     */
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


    /**
     * @brief Returns the deepest common ancestor of two nodes.
     * @return The common ancestor entity, or `entt::null` if the nodes are
     *         not in the same tree.
     */
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


    /// Returns `true` if `ancestor` is a strict ancestor of `descendant`.
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
