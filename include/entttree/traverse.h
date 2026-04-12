/**
 * @file traverse.h
 * @brief Lazy, composable graph traversal framework built on C++20 generators.
 *
 * A Traversal encapsulates a starting node (root) and a successor function
 * that generates children for each node. The traversal itself is abstract —
 * it describes the *structure* of the graph rather than a specific walk order.
 *
 * Traversal adaptors (in the `walk` namespace) compose to filter, prune, map,
 * or reverse the successor structure. Walk functions (`walk::dfs`, `walk::bfs`)
 * flatten a Traversal into a linear Generator of nodes.
 */

#pragma once

#include <deque>
#include <list>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <entttree/defs.h>
#include <entttree/generator.h>

namespace entttree {

/*****************************
 * traversal class + concept *
 *****************************/

/**
 * @brief A lazy description of a rooted graph traversal.
 *
 * By keeping information about the structure of the tree (rather than a flat
 * generator), we can more easily implement transformations and filters on the
 * traversal. See the `walk` namespace for adaptors and walkers.
 *
 * @tparam Node_       The type of each node in the traversal.
 * @tparam Successors_ A callable `(Node&) -> Generator<Node>` that yields
 *                     the children of a given node.
 */
template <typename Node_, typename Successors_>
struct Traversal {
    using Node       = Node_;
    using Successors = Successors_;

    /// The starting point of the traversal, or empty to disable it.
    std::optional<Node> root;

    /**
     * @brief A function returning a generator of the successors of a node.
     *
     * Signature: `Successors(Node&) -> GeneratorConcept<Node>`
     *
     * Any references within the values yielded by the generator must remain
     * valid at least until the generator is destroyed.
     */
    Successors successors;

    /**
     * @brief Conditionally disable the traversal.
     *
     * If `enable` is false, the root is cleared, causing the traversal
     * to produce no nodes. An already-disabled traversal remains disabled.
     */
    Traversal&& enable(bool enable) && {
        if (not enable) root = std::nullopt;
        return std::move(*this);
    }

    /// @copydoc enable(bool)&&
    Traversal& enable(bool enable) & {
        if (not enable) root = std::nullopt;
        return *this;
    }
};


/*****************************
 * concepts + type helpers   *
 *****************************/

/// Strip references/cv to get the bare Traversal type.
template <typename T>
using TraversalValue = std::remove_cvref_t<T>;

/// Extract the Node type from a Traversal (after stripping cvref).
template <typename T>
using TraversalNode = typename TraversalValue<T>::Node;

/// Extract the Successors callable type from a Traversal.
template <typename T>
using TraversalSuccessors = typename TraversalValue<T>::Successors;


/// A type `G` that can be advanced, dereferenced, and tested for exhaustion.
template <typename G, typename T>
concept GeneratorConcept = requires (G g) {
    {   g } -> std::convertible_to<bool>;
    {  *g } -> std::convertible_to<T>;
    { ++g } -> std::convertible_to<G&>;
};


/// Satisfied by types that have a root and a successors function yielding `Node`.
template <typename T, typename Node>
concept TraversalConcept =
requires (TraversalValue<T> t, Node& n) {
    { t.root.has_value() } -> std::convertible_to<bool>;
    { *(t.root) }          -> std::convertible_to<Node>;
    { t.successors(n) }    -> GeneratorConcept<Node>;
};


/// Satisfied by any type that is a Traversal over its own Node type.
template <typename T>
concept AnyTraversalConcept = requires {
    typename TraversalNode<T>;
    typename TraversalSuccessors<T>;
} && TraversalConcept<T, TraversalNode<T>>;


/// The generator type returned by a successor function.
template <typename Successors, typename Node>
using SuccessorGenerator = std::invoke_result_t<Successors&, Node&>;

/// The value type yielded by a successor generator.
template <typename Successors, typename Node>
using SuccessorOutput = decltype(*std::declval<SuccessorGenerator<Successors,Node>&>());

/// The value type yielded by a traversal's successor generator.
template <typename Traversal>
using TraversalOutput = SuccessorOutput<
    TraversalSuccessors<Traversal>,
    TraversalNode<Traversal>
>;

/// The concrete generator type produced by a traversal's successor function.
template <typename Traversal>
using TraversalGenerator = std::remove_cvref_t<
    SuccessorGenerator<
        TraversalSuccessors<Traversal>,
        TraversalNode<Traversal>
    >
>;


/*****************************
 * traversal generation      *
 *****************************/

/**
 * @brief Construct a Traversal from an optional root and a successor function.
 *
 * @param root       The starting node (or empty to create a disabled traversal).
 * @param successors A callable `(Node&) -> Generator<Node>` that yields the
 *                   children of a given node.
 */
template <typename Node, typename Successors>
requires requires (Successors s, Node& n) {
    { s(n) } -> GeneratorConcept<Node>;
}
auto make_traversal(
        std::optional<Node>&& root,
        Successors&& successors)
{
    using S = std::remove_cvref_t<Successors>;
    return Traversal<Node,S> {
        std::move(root),
        std::forward<Successors>(successors)
    };
}


/// @copydoc make_traversal(std::optional<Node>&&, Successors&&)
template <typename Node, typename Successors>
requires requires (Successors s, Node& n) {
    { s(n) } -> GeneratorConcept<Node>;
}
auto make_traversal(
        const std::optional<Node>& root,
        Successors&& successors)
{
    using S = std::remove_cvref_t<Successors>;
    return Traversal<Node,S> {
        root,
        std::forward<Successors>(successors)
    };
}


/// @overload Convenience: wraps a non-optional root in `std::optional`.
template <typename Node, typename Successors>
requires requires (Successors s, std::remove_cvref_t<Node>& n) {
    { s(n) } -> GeneratorConcept<std::remove_cvref_t<Node>>;
}
auto make_traversal(
        Node&& root,
        Successors&& successors)
{
    using N = std::remove_cvref_t<Node>;
    return make_traversal(
        std::make_optional(N{std::forward<Node>(root)}),
        std::forward<Successors>(successors)
    );
}


/*****************************
 * traversal transformations *
 *****************************/

namespace walk {


/*****************************
 * traversal adaptors        *
 *****************************/

/**
 * @brief Prevent certain nodes from generating successors.
 *
 * If `should_explore(node)` returns true, the node's children are explored
 * normally; otherwise the node generates zero successors. The node itself
 * is still visited regardless — to suppress the node entirely, use
 * exclude_if() instead.
 *
 * @param t                The source traversal.
 * @param should_explore   `(const Node&) -> bool`
 */
template <AnyTraversalConcept T, typename Filter>
auto prune_if(T&& t, Filter should_explore) {
    using Traversal = TraversalValue<T>;
    using Node = typename Traversal::Node;
    using G = TraversalGenerator<Traversal>;

    Traversal base = std::forward<T>(t);
    return make_traversal(
        std::move(base.root),
        [
            successors = std::move(base.successors),
            should_explore = std::move(should_explore)
        ] (Node& node) mutable -> MaybeGenerator<G>
        {
            if (should_explore(node)) {
                return MaybeGenerator<G>{ successors(node) };
            }
            return MaybeGenerator<G>{};
        }
    );
}


/**
 * @brief Remove certain nodes from the graph entirely.
 *
 * If `should_admit(node)` returns true, the node is visited; otherwise it
 * is skipped. Excluded nodes are not explored, so their subtrees are also
 * excluded. To visit a node but suppress its children, use prune_if().
 *
 * @param t              The source traversal.
 * @param should_admit   `(const Node&) -> bool`
 */
template <AnyTraversalConcept T, typename Filter>
auto exclude_if(T&& t, Filter should_admit) {
    using Traversal = TraversalValue<T>;
    using Node = typename Traversal::Node;

    Traversal base = std::forward<T>(t);
    std::optional<Node> root;
    if (base.root and should_admit(*base.root)) {
        // Explicit copy/move boundary: traversal stores node handles by value.
        root = std::move(*base.root);
    }

    return make_traversal(
        std::move(root),
        [
            successors   = std::move(base.successors),
            should_admit = std::move(should_admit)
        ] (Node& node) mutable -> Generator<Node>
        {
            for (auto g = successors(node); g; ++g) {
                // Explicit copy/move boundary: successor output is materialized as Node.
                Node child = *g;
                if (should_admit(child)) {
                    co_yield std::move(child);
                }
            }
        }
    );
}


/**
 * @brief Reverse the order of each node's successors.
 *
 * This is a generic operation that buffers all children of each node before
 * yielding them in reverse. If the traversal source can enumerate children
 * in reverse cheaply (e.g. `HierarchySystem::children(..., SiblingOrder::Backward)`),
 * prefer that approach instead.
 */
template <AnyTraversalConcept T>
auto reverse_successors(T&& t) {
    using Traversal = TraversalValue<T>;
    using Node = typename Traversal::Node;

    Traversal base = std::forward<T>(t);
    return make_traversal(
        std::move(base.root),
        [
            successors = std::move(base.successors)
        ] (Node& node) mutable -> Generator<Node>
        {
            // Generic reverse requires buffering children first.
            std::vector<Node> children;
            for (auto g = successors(node); g; ++g) {
                children.emplace_back(*g);
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                co_yield std::move(*it);
            }
        }
    );
}


/**
 * @brief Transform the node type of a traversal.
 *
 * Applies `xform(Node&) -> Value` to every node, producing a new Traversal
 * with node type `Value`. Common uses include extracting fields from a node
 * or augmenting it with additional computed data.
 *
 * @param t      The source traversal.
 * @param xform  `(Node&) -> Value`
 */
template <AnyTraversalConcept Traversal, typename Transform>
auto map_nodes(Traversal&& t, Transform&& xform) {
    using T = TraversalValue<Traversal>;
    using Node = typename T::Node;
    // map_nodes owns transformed nodes by value, so reference returns are decayed.
    using Value = std::remove_cvref_t<std::invoke_result_t<Transform&, Node&>>;

    T base = std::forward<Traversal>(t);
    std::optional<Value> root_value;
    if (base.root) {
        root_value = std::make_optional(xform(*base.root));
    }

    return make_traversal(
        std::move(root_value),
        [
            successors = std::move(base.successors),
            xform      = std::forward<Transform>(xform)
        ]
            (Value& node) mutable -> Generator<Value>
        {
            for (auto g = successors(node); g; ++g) {
                auto&& child = *g;
                co_yield xform(child);
            }
        }
    );
}


/*****************************
 * traversal walkers         *
 *****************************/

/// @cond INTERNAL
namespace detail {

template <typename Node, typename SuccessorGenerator>
struct DfsEntry {
    Node               node;
    SuccessorGenerator successors;

    template <typename N, typename Successors>
    DfsEntry(N&& n, Successors& next):
        node(std::forward<N>(n)),
        successors(next(node)) {}
};


template <DfsOrder recursion_order, typename Node, typename Successors>
Generator<Node> dfs(
        std::optional<Node> root,
        Successors          successors)
{
    using SuccessorGenerator = std::remove_cvref_t<
        std::invoke_result_t<Successors&, Node&>
    >;
    using StackEntry = DfsEntry<Node, SuccessorGenerator>;

    if (not root) co_return;

    std::list<StackEntry> stack;
    stack.emplace_back(std::move(*root), successors);

    if constexpr (recursion_order == DfsOrder::ShallowFirst) {
        co_yield stack.back().node;
    }

    while (not stack.empty()) {
        StackEntry& entry = stack.back();
        if (entry.successors) {
            // Explicit copy/move boundary: child is stored in traversal frame.
            Node next_node = *entry.successors;
            ++entry.successors;

            stack.emplace_back(
                std::move(next_node),
                successors
            );
            if constexpr (recursion_order == DfsOrder::ShallowFirst) {
                co_yield stack.back().node;
            }
        } else {
            if constexpr (recursion_order == DfsOrder::DeepFirst) {
                co_yield entry.node;
            }
            stack.pop_back();
        }
    }
}


template <typename Node, typename Successors>
Generator<Node> bfs(
        std::optional<Node> root,
        Successors          successors)
{
    if (not root) co_return;

    std::deque<Node> queue;
    queue.emplace_back(std::move(*root));

    while (not queue.empty()) {
        Node node = std::move(queue.front());
        queue.pop_front();

        for (auto g = successors(node); g; ++g) {
            // Explicit copy/move boundary: queue stores node handles by value.
            Node child = *g;
            queue.emplace_back(std::move(child));
        }

        co_yield std::move(node);
    }
}

}  // namespace detail
/// @endcond


/**
 * @brief Flatten a Traversal into a depth-first Generator.
 *
 * The walk order is selected at compile time via the template parameter.
 * Uses an explicit stack (not recursion) for efficiency.
 *
 * @tparam recursion_order `DfsOrder::ShallowFirst` (pre-order, default) or
 *                         `DfsOrder::DeepFirst` (post-order).
 */
template <
    DfsOrder recursion_order = DfsOrder::ShallowFirst,
    AnyTraversalConcept T
>
auto dfs(T&& t) {
    using Traversal = TraversalValue<T>;
    using Node = typename Traversal::Node;

    Traversal base = std::forward<T>(t);
    return detail::dfs<recursion_order, Node>(
        std::move(base.root),
        std::move(base.successors)
    );
}


/// @overload Runtime-selected DFS order.
template <AnyTraversalConcept T>
auto dfs(
        T&&            t,
        DfsOrder recursion_order)
{
    if (recursion_order == DfsOrder::ShallowFirst) {
        return dfs<DfsOrder::ShallowFirst>(std::forward<T>(t));
    }
    return dfs<DfsOrder::DeepFirst>(std::forward<T>(t));
}


/**
 * @brief Flatten a Traversal into a breadth-first Generator.
 *
 * Nodes are visited level by level, using an internal queue.
 */
template <AnyTraversalConcept T>
auto bfs(T&& t) {
    using Traversal = TraversalValue<T>;
    using Node = typename Traversal::Node;

    Traversal base = std::forward<T>(t);
    return detail::bfs<Node>(
        std::move(base.root),
        std::move(base.successors)
    );
}

}  // namespace walk


/*****************************
 * utility transformation    *
 *****************************/

/**
 * @brief Transform a traversal inductively, threading parent context through successors.
 *
 * Changes the traversal from node type `Node` to `Value`, where each child's
 * value is computed from its parent's value and the original child node.
 *
 * @param t               The source traversal.
 * @param make_successor  `(optional<Value>& parent, Node&& child) -> optional<Value>`.
 *                        The root's parent is `std::nullopt`. If the function returns
 *                        `std::nullopt`, the node is skipped.
 * @param get_node        `(Value&) -> Node&` extracts the original node from a Value
 *                        so the inner successor function can operate on it.
 */
template <
    typename Node,
    typename Value,
    typename Trav,
    typename Compose,
    typename Get
>
requires TraversalConcept<Trav, Node>
      and requires (Compose c, std::optional<Value>& parent, Node n) {
          { c(parent, std::move(n)) } -> std::convertible_to<std::optional<Value>>;
      }
      and std::invocable<Get&, Value&>
auto inductive_transform(Trav&& t, Compose&& make_successor, Get&& get_node) {
    using Traversal = TraversalValue<Trav>;

    Traversal base = std::forward<Trav>(t);

    std::optional<Value> root_value;
    if (base.root) {
        std::optional<Value> no_parent = std::nullopt;
        Node root_node = *base.root;
        root_value = make_successor(no_parent, std::move(root_node));
    }

    return make_traversal(
        std::move(root_value),
        [
            successors = std::move(base.successors),
            get_node = std::forward<Get>(get_node),
            make_successor = std::forward<Compose>(make_successor)
        ] (Value& parent) mutable -> Generator<Value> {
            auto&& parent_node = get_node(parent);
            std::optional<Value> parent_value = parent;
            for (auto g = successors(parent_node); g; ++g) {
                Node child = *g;
                std::optional<Value> value = make_successor(
                    parent_value,
                    std::move(child)
                );
                if (value) co_yield std::move(*value);
            }
        }
    );
}


}  // namespace entttree
