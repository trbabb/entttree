#pragma once

#include <list>
#include <type_traits>

#include <theta-hierarchy/defs.h>
#include <theta-hierarchy/generator.h>

namespace theta {

/*****************************
 * traversal class + concept *
 *****************************/

template <typename Node_, typename Successors_>
struct Traversal {
    using Node       = Node_;
    using Successors = Successors_;

    std::optional<Node> root;
    Successors successors;

    Traversal&& enable(bool enable) && {
        if (not enable) root = std::nullopt;
        return std::move(*this);
    }

    Traversal& enable(bool enable) & {
        if (not enable) root = std::nullopt;
        return *this;
    }
};


/*****************************
 * concepts                  *
 *****************************/

template <typename G, typename T>
concept GeneratorConcept = requires (G g) {
    {   g } -> std::convertible_to<bool>;
    {  *g } -> std::convertible_to<T>;
    { ++g } -> std::convertible_to<G&>;
};

template <typename T, typename Node>
concept TraversalConcept =
requires (T t, Node& n) {
    { *(t.root) }       -> std::convertible_to<Node>;
    { t.successors(n) } -> GeneratorConcept<Node>;
};

template <typename T>
concept AnyTraversalConcept = requires (T t) {
    { t } -> TraversalConcept<typename T::Node>;
};

template <typename Successors, typename Node>
using SuccessorGenerator = std::invoke_result_t<Successors, Node>;

template <typename Successors, typename Node>
using SuccessorOutput = decltype(*std::declval<SuccessorGenerator<Successors,Node>>());

template <typename Traversal>
using TraversalOutput = SuccessorOutput<
    typename Traversal::Successors,
    typename Traversal::Node
>;

template <typename Traversal>
using TraversalGenerator = std::remove_cvref_t<
    std::invoke_result_t<
        typename Traversal::Successors,
        typename Traversal::Node
    >
>;


/*****************************
 * traversal generation      *
 *****************************/

template <typename Node, typename Successors>
requires requires (Successors s, Node n) {
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

template <typename Node, typename Successors>
requires requires (Successors s, Node n) {
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

template <typename Node, typename Successors>
requires requires (Successors s, Node n) {
    { s(n) } -> GeneratorConcept<Node>;
}
auto make_traversal(
        Node&& root,
        Successors&& successors)
{
    return make_traversal(
        std::make_optional(std::forward<Node>(root)),
        std::forward<Successors>(successors)
    );
}

/*****************************
 * traversal transformations *
 *****************************/

template <AnyTraversalConcept T, typename Filter>
auto prune(T&& t, Filter should_explore) {
    return make_traversal(
        std::forward<T>(t).root,
        [=,
            successors     = std::forward<T>(t).successors,
            should_explore = std::move(should_explore)
        ] (auto& node)
        {
            using G = TraversalGenerator<T>;
            if (should_explore(node)) {
                return MaybeGenerator<G>{ successors(node) };
            } else {
                return MaybeGenerator<G>{};
            }
        }
    );
}


template <AnyTraversalConcept T, typename Filter>
auto filter(T&& t, Filter should_admit) {
    using Node = typename T::Node;
    return make_traversal(
        std::forward<T>(t).root,
        [=,
            successors   = std::forward<T>(t).successors,
            should_admit = std::move(should_admit)
        ] (auto& node) -> Generator<Node>
        {
            for (auto g = successors(node); g; ++g) {
                auto n = *g;
                if (should_admit(n)) {
                    co_yield n;
                }
            }
        }
    );
}


template <AnyTraversalConcept Traversal, typename Transform>
auto transform(Traversal&& t, Transform&& xform) {
    using Node = typename Traversal::Node;
    using Value = std::remove_cvref_t<std::invoke_result_t<Transform, Node&>>;
    std::optional<Value> root_value;
    if (t.root) {
        auto tmp_root = *std::forward<Traversal>(t).root;
        root_value = std::make_optional(xform(tmp_root));
    }
    return make_traversal(
        std::move(root_value),
        [=,
            successors = std::forward<Traversal>(t).successors,
            xform      = std::forward<Transform>(xform)
        ]
            (auto& node) -> Generator<Value>
        {
            for (auto g = successors(node); g; ++g) {
                co_yield xform(*g);
            }
        }
    );
}


template <
    typename Node,
    typename Value,
    TraversalConcept<Node> Trav,
    std::invocable<std::optional<Value>&, Node&&> Compose,
    std::invocable<Value&> Get
>
requires requires (Compose c, Node n) {
    { c(std::nullopt, std::move(n)) } -> std::convertible_to<std::optional<Value>>;
}
auto inductive_transform(Trav&& t, Compose&& make_successor, Get&& get_node) {
    return make_traversal(
        t.root
            ? make_successor(std::nullopt, *std::forward<Trav>(t).root)
            : std::nullopt,
        [
            successors = std::forward<Trav>(t).successors,
            get_node,
            make_successor
        ] (Value& parent) -> Generator<Value> {
            auto&& parent_node = get_node(parent);
            for (auto g = successors(parent_node); g; ++g) {
                auto child = *g;
                std::optional<Value> value = make_successor(
                    parent,
                    std::move(child)
                );
                if (value) co_yield std::move(*value);
            }
        }
    );
}


/*****************************
 * traverse functions        *
 *****************************/

namespace detail {

template <typename Node, typename SuccessorGenerator>
struct StackEntry {
    Node               node;
    SuccessorGenerator successors;

    template <typename N, typename Successors>
    StackEntry(N&& n, Successors& successors):
        node(std::forward<N>(n)),
        successors(successors(node)) {}
};

}  // namespace detail


template <typename Node, typename Successors>
Generator<Node> traverse_dfs(
        std::optional<Node> root,
        RecursionOrder      recursion_order,
        Successors          successors)
{
    using SuccessorGenerator = std::remove_cvref_t<
        std::invoke_result_t<Successors, Node&>
    >;
    using StackEntry = detail::StackEntry<Node, SuccessorGenerator>;

    if (not root) co_return;

    std::list<StackEntry> stack;
    stack.emplace_back(std::move(*root), successors);

    if (recursion_order == RecursionOrder::ShallowFirst) {
        co_yield stack.back().node;
    }

    while (not stack.empty()) {
        StackEntry& entry = stack.back();
        if (entry.successors) {
            auto&& next_node = *entry.successors;
            StackEntry& next = stack.emplace_back(
                next_node,
                successors
            );
            if (recursion_order == RecursionOrder::ShallowFirst) {
                co_yield next.node;
            }
            ++entry.successors;
        } else {
            if (recursion_order == RecursionOrder::DeepFirst) {
                co_yield entry.node;
            }
            stack.pop_back();
        }
    }
}


template <AnyTraversalConcept T>
auto traverse_dfs(T&& t, RecursionOrder recursion_order) {
    return traverse_dfs(
        std::forward<T>(t).root,
        recursion_order,
        std::forward<T>(t).successors
    );
}


template <typename Node, typename Successors>
Generator<Node> traverse_dfs(
        Node                 root,
        RecursionOrder      recursion_order,
        Successors          successors)
{
    return traverse_dfs(
        std::make_optional(std::move(root)),
        recursion_order,
        successors
    );
}


}  // namespace theta
