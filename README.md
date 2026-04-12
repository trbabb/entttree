# entttree

A C++20 library for hierarchical entity management built on [EnTT](https://github.com/skypjack/entt). Provides type-tagged parent-child hierarchies, affine transform propagation, and hierarchical bounding volumes with lazy bound recomputation.

The hierarchy structure is defined by a single per-entity component `ParentConnection` associated with each node. The component names its parent entity and encodes the child's position among its siblings using a fractional index. All other information is derived efficiently from the `entity : ParentConnection` mapping, and all changes to the graph structure can be written as updates to that one component.

Multiple independent hierarchies can coexist on the same entities using compile-time tag types:

```cpp
struct RenderH {};
struct CollisionH {};

entt::registry reg;
entttree::HierarchySystem<RenderH>    scene(reg);
entttree::HierarchySystem<CollisionH> collision(reg);
```

Transform and bounds layers are also independently taggable per hierarchy (with defaults), so one hierarchy can host multiple overlays such as render/collision/hitbox bounds.

## Documentation

[API Reference](https://trbabb.github.io/entttree/)

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
for (auto g = entttree::walk::dfs(h.traverse(root, SiblingOrder::Forward)); g; ++g) {
    // visit nodes depth-first
}

// signals
entt::sink{h.on_added}.connect<[] (entt::entity child, ParentConnection<RenderH> connection) { ... }>();
entt::sink{h.on_removed}.connect<[] (entt::entity child, ParentConnection<RenderH> old_connection) { ... }>();
entt::sink{h.on_changed}.connect<[] (entt::entity child, ParentConnection<RenderH> old_val, ParentConnection<RenderH> new_val) { ... }>();
```

### TransformSystem

Layers affine transforms on a hierarchy. Nodes without an explicit transform use the identity.

```cpp
auto xf = entttree::add_transforms(reg, h);  // defaults: double, 2D, layer tag = RenderH

xf.set_transform(child, AffineTransform<double,2>::translation({5, 0}));

auto world = xf.object_to_world(child);   // accumulated transform to root
auto rel   = xf.xf_between(node_a, node_b);
```

To maintain multiple transform overlays on one hierarchy:

```cpp
struct RenderXf {};
struct PhysicsXf {};

// create two separate TransformSystems on the same hierarchy
auto render_xf  = entttree::add_transforms<RenderXf>(reg, h);
auto physics_xf = entttree::add_transforms<PhysicsXf>(reg, h);
```

### BoundsSystem

Maintains hierarchical bounding boxes. Computed bounds are the union of a node's intrinsic bounds and its children's bounds (in parent space). Listens to hierarchy and transform signals for automatic dirty propagation.

```cpp
auto bs = entttree::add_bounds(reg, xf);  // defaults: bounds layer = hierarchy tag

bs.set_intrinsic_bounds(entity, Rect<double,2>{{0,0}, {10,10}});

auto bounds = bs.get_computed_bounds(entity);  // recomputes if dirty

// spatial queries
for (auto g = bs.search_under_point(root, fwd, shallow_first, point); g; ++g) {
    // nodes whose intrinsic bounds contain the point
}
```

Multiple bounds overlays on one hierarchy can share the same transform system:

```cpp
struct RenderBounds {};
struct CollisionBounds {};
struct HitboxBounds {};

auto render_bounds    = entttree::add_bounds<RenderBounds>(reg, xf);
auto collision_bounds = entttree::add_bounds<CollisionBounds>(reg, xf);
auto hitbox_bounds    = entttree::add_bounds<HitboxBounds>(reg, xf);
```

To mix multiple bounds layers with multiple transform layers on one hierarchy:

```cpp
auto render_xf  = entttree::add_transforms<RenderXf>(reg, h);
auto physics_xf = entttree::add_transforms<PhysicsXf>(reg, h);

auto render_bounds    = entttree::add_bounds<RenderBounds>(reg, render_xf);
auto collision_bounds = entttree::add_bounds<CollisionBounds>(reg, physics_xf);
```

## Traversal framework

Traversals are lazy, coroutine-based generators that can be composed:

```cpp
auto t = h.traverse(root, SiblingOrder::Forward);

// filter nodes out of the graph entirely
auto visible = entttree::walk::exclude_if(t, [](const auto& node) {
    return is_visible(node);
});

// prune descendants, but still visit the node itself
auto in_frustum = entttree::walk::prune_if(visible, [](const auto& node) {
    return should_explore(node);
});

// choose a walker + visit order
for (auto g = entttree::walk::dfs(
         in_frustum,
         DfsOrder::ShallowFirst); g; ++g) {
    // ...
}

// compile-time options are available too
for (auto g = entttree::walk::dfs<
         DfsOrder::DeepFirst>(in_frustum); g; ++g) {
    // ...
}

// sibling order is controlled by the traversal source
auto backward = h.traverse(root, SiblingOrder::Backward);
for (auto g = entttree::walk::dfs(backward); g; ++g) {
    // ...
}

// generic reversal is possible when the source cannot do it cheaply
auto reversed = entttree::walk::reverse_successors(in_frustum);
for (auto g = entttree::walk::bfs(reversed); g; ++g) {
    // ...
}
```

### Traversal node semantics

Traversal nodes are intentionally stored and queued by value. To avoid expensive copies,
`Node` should normally be a lightweight handle (entity ID, pointer, index, or
`std::reference_wrapper<T>`), not a heavyweight component payload.

```cpp
// good: cheap handle traversal
auto t = entttree::make_traversal(
    root_entity,
    [&h] (entt::entity& e) { return h.children(e, SiblingOrder::Forward); }
);

// augment without copying components
struct NodeView {
    entt::entity eid;
    const Renderable* render;
    const LocalTransform<RenderH,double,2>* xf;
};

auto view = entttree::walk::map_nodes(t, [&reg] (entt::entity& eid) -> NodeView {
    return {
        eid,
        reg.try_get<Renderable>(eid),
        reg.try_get<LocalTransform<RenderH,double,2>>(eid)
    };
});
```

If you need stable reference semantics during traversal, do not mutate data structures
in ways that invalidate those references while the generator is active.

`walk::reverse_successors(...)` buffers each expanded node's child list in a temporary
vector. If your source can already enumerate children in reverse cheaply (like
`HierarchySystem::children(..., SiblingOrder::Backward)`), prefer that.

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
