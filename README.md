# entttree

A C++20 library for hierarchical entity management built on [EnTT](https://github.com/skypjack/entt). Provides type-tagged parent-child hierarchies, affine transform propagation, and hierarchical bounding volumes with automatic dirty tracking.

Multiple independent hierarchies can coexist on the same entities using compile-time tag types:

```cpp
struct RenderH {};
struct CollisionH {};

entt::registry reg;
entttree::HierarchySystem<RenderH>    scene(reg);
entttree::HierarchySystem<CollisionH> collision(reg);
```

## Systems

### HierarchySystem

Manages parent-child relationships with fractional-index ordering and typed signal notifications.

```cpp
entttree::HierarchySystem<RenderH> h(reg);

auto root  = reg.create();
auto child = reg.create();

h.set_parent(child, root);               // add child under root
h.set_parent(child, root, position);      // ...at a specific position
h.order_child_before(child_a, child_b);   // reorder siblings

// traversal
for (auto g = h.traverse_dfs(root); g; ++g) {
    // visit nodes depth-first
}

// signals
h.on_added.connect([](entt::entity child, auto& connection) { ... });
h.on_removed.connect([](entt::entity child, auto& old_connection) { ... });
h.on_changed.connect([](entt::entity child, auto& old_val, auto& new_val) { ... });
```

### TransformSystem

Layers affine transforms on a hierarchy. Nodes without an explicit transform use the identity.

```cpp
entttree::TransformSystem<RenderH, double, 2> xf(reg, h);

xf.set_transform(child, AffineTransform<double,2>::translation({5, 0}));

auto world = xf.object_to_world(child);   // accumulated transform to root
auto rel   = xf.xf_between(node_a, node_b);
```

### BoundsSystem

Maintains hierarchical bounding boxes. Computed bounds are the union of a node's intrinsic bounds and its children's bounds (in parent space). Listens to hierarchy and transform signals for automatic dirty propagation.

```cpp
entttree::BoundsSystem<RenderH, double, 2> bs(reg, xf);

bs.set_intrinsic_bounds(entity, Rect<double,2>{{0,0}, {10,10}});

auto bounds = bs.get_computed_bounds(entity);  // recomputes if dirty

// spatial queries
for (auto g = bs.search_under_point(root, fwd, shallow_first, point); g; ++g) {
    // nodes whose intrinsic bounds contain the point
}
```

## Traversal framework

Traversals are lazy, coroutine-based generators that can be composed:

```cpp
auto t = h.traverse(root, SiblingTraversalOrder::Forward);

// add transforms
auto with_xf = xf.augment_with_transforms(t, [](auto& n) { return n.node_id; });

// filter branches
auto pruned = entttree::prune(t, [](auto& node) { return should_explore(node); });

// depth-first walk
for (auto g = entttree::traverse_dfs(t, RecursionOrder::ShallowFirst); g; ++g) {
    // ...
}
```

## Dependencies

- [EnTT](https://github.com/skypjack/entt) -- entity-component system
- [geomc](https://github.com/trbabb/geomc) -- geometric types (AffineTransform, Rect, Vec, Ray)
- [ankerl/unordered_dense](https://github.com/martinus/unordered_dense) -- high-performance hash maps

## Building

Requires CMake 3.20+ and a C++20 compiler.

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --install build --prefix /usr/local
```

Downstream projects can then use:

```cmake
find_package(entttree)
target_link_libraries(myapp PRIVATE entttree::entttree)
```

### Options

| Option | Default | Description |
|---|---|---|
| `ENTTTREE_BUILD_TESTS` | `ON` | Build the test suite |
