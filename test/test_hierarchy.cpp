#include <cassert>
#include <iostream>
#include <vector>

#include <entttree/bounding_hierarchy.h>

using namespace entttree;

// hierarchy tags
struct SceneH {};
struct CollisionH {};
struct UiH {};

// optional transform/bounds layer tags
struct RenderXf {};
struct PhysicsXf {};
struct RenderBounds {};
struct CollisionBounds {};
struct HitboxBounds {};


void test_basic_hierarchy() {
    std::cout << "test_basic_hierarchy... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto root  = reg.create();
    auto child = reg.create();
    auto grandchild = reg.create();

    h.set_parent(child, root);
    h.set_parent(grandchild, child);

    assert(h.parent_of(child) == root);
    assert(h.parent_of(grandchild) == child);
    assert(h.child_count(root) == 1);
    assert(h.child_count(child) == 1);
    assert(h.is_ancestor_of(root, grandchild));
    assert(!h.is_ancestor_of(grandchild, root));

    std::cout << "ok\n";
}


void test_multiple_children_ordering() {
    std::cout << "test_multiple_children_ordering... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto root = reg.create();
    auto a = reg.create();
    auto b = reg.create();
    auto c = reg.create();

    h.set_parent(a, root);
    h.set_parent(b, root);
    h.set_parent(c, root);

    // children should be in insertion order
    std::vector<entt::entity> kids;
    for (auto g = h.children(root, SiblingOrder::Forward); g; ++g) {
        kids.push_back(g->node_id);
    }
    assert(kids.size() == 3);
    assert(kids[0] == a);
    assert(kids[1] == b);
    assert(kids[2] == c);

    // reorder: move c before a
    h.order_child_before(c, a);
    kids.clear();
    for (auto g = h.children(root, SiblingOrder::Forward); g; ++g) {
        kids.push_back(g->node_id);
    }
    assert(kids[0] == c);
    assert(kids[1] == a);
    assert(kids[2] == b);

    std::cout << "ok\n";
}


void test_reparenting() {
    std::cout << "test_reparenting... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto p1 = reg.create();
    auto p2 = reg.create();
    auto child = reg.create();

    h.set_parent(child, p1);
    assert(h.parent_of(child) == p1);
    assert(h.child_count(p1) == 1);

    h.set_parent(child, p2);
    assert(h.parent_of(child) == p2);
    assert(h.child_count(p1) == 0);
    assert(h.child_count(p2) == 1);

    std::cout << "ok\n";
}


void test_unparent() {
    std::cout << "test_unparent... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto root = reg.create();
    auto child = reg.create();

    h.set_parent(child, root);
    assert(h.child_count(root) == 1);

    auto old = h.unparent(child);
    assert(old.has_value());
    assert(old->parent == root);
    assert(h.child_count(root) == 0);
    assert(h.parent_of(child) == entt::null);

    std::cout << "ok\n";
}


void test_signals() {
    std::cout << "test_signals... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    struct Counters {
        int added = 0;
        int changed = 0;
        int removed = 0;
    };
    Counters counters;
    using PC = ParentConnection<SceneH>;

    entt::sink{h.on_added}.connect<[] (Counters& c, entt::entity, PC) {
        ++c.added;
    }>(counters);
    entt::sink{h.on_changed}.connect<[] (Counters& c, entt::entity, PC, PC) {
        ++c.changed;
    }>(counters);
    entt::sink{h.on_removed}.connect<[] (Counters& c, entt::entity, PC) {
        ++c.removed;
    }>(counters);

    auto root = reg.create();
    auto child = reg.create();
    auto other = reg.create();

    h.set_parent(child, root);
    assert(counters.added == 1);

    h.set_parent(child, other);  // reparent
    assert(counters.changed == 1);

    h.unparent(child);
    assert(counters.removed == 1);

    std::cout << "ok\n";
}


void test_traversal() {
    std::cout << "test_traversal... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    auto a1   = reg.create();
    auto a2   = reg.create();

    h.set_parent(a, root);
    h.set_parent(b, root);
    h.set_parent(a1, a);
    h.set_parent(a2, a);

    // DFS preorder: root, a, a1, a2, b
    std::vector<entt::entity> visited;
    for (auto g = walk::dfs(
            h.traverse(root, SiblingOrder::Forward),
            DfsOrder::ShallowFirst); g; ++g) {
        visited.push_back(g->node_id);
    }
    assert(visited.size() == 5);
    assert(visited[0] == root);
    assert(visited[1] == a);
    assert(visited[2] == a1);
    assert(visited[3] == a2);
    assert(visited[4] == b);

    std::cout << "ok\n";
}


void test_traversal_adaptors_and_walkers() {
    std::cout << "test_traversal_adaptors_and_walkers... ";

    auto t = make_traversal(
        0,
        [] (int& node) -> Generator<int> {
            switch (node) {
                case 0: co_yield 1; co_yield 2; break;
                case 1: co_yield 3; co_yield 4; break;
                case 2: co_yield 5; co_yield 6; break;
                default: break;
            }
        }
    );

    auto only_even = walk::exclude_if(t, [] (int node) {
        return (node % 2) == 0;
    });
    std::vector<int> even_pre;
    for (auto g = walk::dfs(only_even, DfsOrder::ShallowFirst); g; ++g) {
        even_pre.push_back(*g);
    }
    assert((even_pre == std::vector<int>{0, 2, 6}));

    auto no_root = walk::exclude_if(t, [] (int node) {
        return node != 0;
    });
    std::vector<int> excluded_root;
    for (auto g = walk::dfs(no_root); g; ++g) {
        excluded_root.push_back(*g);
    }
    assert(excluded_root.empty());

    auto stop_at_root = walk::prune_if(t, [] (int node) {
        return node != 0;
    });
    std::vector<int> pruned_root;
    for (auto g = walk::dfs(stop_at_root); g; ++g) {
        pruned_root.push_back(*g);
    }
    assert((pruned_root == std::vector<int>{0}));

    std::vector<int> bfs_order;
    for (auto g = walk::bfs(t); g; ++g) {
        bfs_order.push_back(*g);
    }
    assert((bfs_order == std::vector<int>{0, 1, 2, 3, 4, 5, 6}));

    auto reversed = walk::reverse_successors(t);
    std::vector<int> dfs_backward;
    for (auto g = walk::dfs(reversed); g; ++g) {
        dfs_backward.push_back(*g);
    }
    assert((dfs_backward == std::vector<int>{0, 2, 6, 5, 1, 4, 3}));

    std::vector<int> bfs_backward;
    for (auto g = walk::bfs(reversed); g; ++g) {
        bfs_backward.push_back(*g);
    }
    assert((bfs_backward == std::vector<int>{0, 2, 1, 6, 5, 4, 3}));

    std::cout << "ok\n";
}


void test_dca_and_path() {
    std::cout << "test_dca_and_path... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);

    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    auto a1   = reg.create();
    auto b1   = reg.create();

    h.set_parent(a, root);
    h.set_parent(b, root);
    h.set_parent(a1, a);
    h.set_parent(b1, b);

    assert(h.deepest_common_ancestor(a1, b1) == root);
    assert(h.deepest_common_ancestor(a, a1) == a);

    TreePath p = h.path(a1);
    assert(p.size() == 3);
    assert(p[0] == root);
    assert(p[1] == a);
    assert(p[2] == a1);

    std::cout << "ok\n";
}


void test_multiple_hierarchies() {
    std::cout << "test_multiple_hierarchies... ";

    entt::registry reg;
    HierarchySystem<SceneH> scene(reg);
    HierarchySystem<CollisionH> collision(reg);

    auto a = reg.create();
    auto b = reg.create();
    auto c = reg.create();

    // different hierarchies for the same entities
    scene.set_parent(b, a);
    scene.set_parent(c, a);

    collision.set_parent(a, c);  // reversed!
    collision.set_parent(b, c);

    assert(scene.parent_of(b) == a);
    assert(collision.parent_of(b) == c);
    assert(scene.child_count(a) == 2);
    assert(collision.child_count(c) == 2);

    std::cout << "ok\n";
}

void test_default_template_params() {
    std::cout << "test_default_template_params... ";

    using R2 = Rect<double,2>;
    using V2 = Vec<double,2>;

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH> xf(reg, h); // defaults: double, 2D, transform layer = SceneH
    BoundsSystem<SceneH> bs(reg, xf);   // defaults: double, 2D, bounds layer = SceneH

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);
    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{1,1}});

    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 0.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 1.0) < 1e-10);
    assert(std::abs(rb->lo[1] - 0.0) < 1e-10);
    assert(std::abs(rb->hi[1] - 1.0) < 1e-10);

    std::cout << "ok\n";
}


void test_multiple_transform_layers_on_one_hierarchy() {
    std::cout << "test_multiple_transform_layers_on_one_hierarchy... ";

    using V2 = Vec<double,2>;

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2, RenderXf>  render_xf(reg, h);
    TransformSystem<SceneH, double, 2, PhysicsXf> physics_xf(reg, h);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    render_xf.set_transform(child, translation(V2{5.0, 0.0}));
    physics_xf.set_transform(child, translation(V2{20.0, 0.0}));

    V2 origin{0.0, 0.0};
    V2 render_world  = render_xf.object_to_world(child) * origin;
    V2 physics_world = physics_xf.object_to_world(child) * origin;

    assert(std::abs(render_world[0] - 5.0) < 1e-10);
    assert(std::abs(physics_world[0] - 20.0) < 1e-10);
    assert((reg.try_get<LocalTransform<SceneH,double,2,RenderXf>>(child) != nullptr));
    assert((reg.try_get<LocalTransform<SceneH,double,2,PhysicsXf>>(child) != nullptr));

    std::cout << "ok\n";
}


void test_multiple_bounds_layers_on_one_hierarchy() {
    std::cout << "test_multiple_bounds_layers_on_one_hierarchy... ";

    using R2 = Rect<double,2>;
    using V2 = Vec<double,2>;

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH> xf(reg, h);
    BoundsSystem<SceneH, double, 2, RenderBounds>    render_bounds(reg, xf);
    BoundsSystem<SceneH, double, 2, CollisionBounds> collision_bounds(reg, xf);
    BoundsSystem<SceneH, double, 2, HitboxBounds>    hitbox_bounds(reg, xf);

    auto p1    = reg.create();
    auto p2    = reg.create();
    auto child = reg.create();
    h.set_parent(child, p1);
    xf.set_transform(child, translation(V2{0.0, 0.0}));

    render_bounds.set_intrinsic_bounds(child,    R2{V2{ 0, 0}, V2{ 1, 1}});
    collision_bounds.set_intrinsic_bounds(child, R2{V2{10, 0}, V2{12, 1}});
    hitbox_bounds.set_intrinsic_bounds(child,    R2{V2{-1,-1}, V2{ 2, 2}});

    auto r1 = render_bounds.get_computed_bounds(p1);
    auto c1 = collision_bounds.get_computed_bounds(p1);
    auto h1 = hitbox_bounds.get_computed_bounds(p1);
    assert(r1.has_value() && c1.has_value() && h1.has_value());
    assert(std::abs(r1->lo[0] - 0.0) < 1e-10 && std::abs(r1->hi[0] - 1.0) < 1e-10);
    assert(std::abs(c1->lo[0] - 10.0) < 1e-10 && std::abs(c1->hi[0] - 12.0) < 1e-10);
    assert(std::abs(h1->lo[0] + 1.0) < 1e-10 && std::abs(h1->hi[0] - 2.0) < 1e-10);

    h.set_parent(child, p2);
    assert(!render_bounds.get_computed_bounds(p1).has_value());
    assert(!collision_bounds.get_computed_bounds(p1).has_value());
    assert(!hitbox_bounds.get_computed_bounds(p1).has_value());
    assert(render_bounds.get_computed_bounds(p2).has_value());
    assert(collision_bounds.get_computed_bounds(p2).has_value());
    assert(hitbox_bounds.get_computed_bounds(p2).has_value());

    xf.set_transform(child, translation(V2{100.0, 0.0}));

    auto r2 = render_bounds.get_computed_bounds(p2);
    auto c2 = collision_bounds.get_computed_bounds(p2);
    auto h2 = hitbox_bounds.get_computed_bounds(p2);
    assert(r2.has_value() && c2.has_value() && h2.has_value());
    assert(std::abs(r2->lo[0] - 100.0) < 1e-10 && std::abs(r2->hi[0] - 101.0) < 1e-10);
    assert(std::abs(c2->lo[0] - 110.0) < 1e-10 && std::abs(c2->hi[0] - 112.0) < 1e-10);
    assert(std::abs(h2->lo[0] - 99.0)  < 1e-10 && std::abs(h2->hi[0] - 102.0) < 1e-10);

    std::cout << "ok\n";
}


void test_separate_hierarchies_are_independent_with_layers() {
    std::cout << "test_separate_hierarchies_are_independent_with_layers... ";

    using R2 = Rect<double,2>;
    using V2 = Vec<double,2>;

    entt::registry reg;
    HierarchySystem<SceneH> scene_h(reg);
    HierarchySystem<UiH> ui_h(reg);

    TransformSystem<SceneH, double, 2, RenderXf> scene_xf(reg, scene_h);
    TransformSystem<UiH, double, 2, RenderXf>    ui_xf(reg, ui_h);

    BoundsSystem<SceneH, double, 2, RenderBounds, RenderXf> scene_bounds(reg, scene_xf);
    BoundsSystem<UiH, double, 2, RenderBounds, RenderXf>    ui_bounds(reg, ui_xf);

    auto scene_root  = reg.create();
    auto scene_child = reg.create();
    auto ui_root     = reg.create();
    auto ui_child    = reg.create();

    scene_h.set_parent(scene_child, scene_root);
    ui_h.set_parent(ui_child, ui_root);

    scene_bounds.set_intrinsic_bounds(scene_child, R2{V2{0,0}, V2{10,10}});
    auto scene_rb = scene_bounds.get_computed_bounds(scene_root);
    assert(scene_rb.has_value());
    assert(!ui_bounds.get_computed_bounds(ui_root).has_value());

    ui_bounds.set_intrinsic_bounds(ui_child, R2{V2{50,50}, V2{60,60}});
    auto ui_rb = ui_bounds.get_computed_bounds(ui_root);
    assert(ui_rb.has_value());

    // mutating UI hierarchy should not affect scene bounds
    ui_h.unparent(ui_child);
    assert(!ui_bounds.get_computed_bounds(ui_root).has_value());
    scene_rb = scene_bounds.get_computed_bounds(scene_root);
    assert(scene_rb.has_value());
    assert(std::abs(scene_rb->hi[0] - 10.0) < 1e-10);

    std::cout << "ok\n";
}


void test_transforms() {
    std::cout << "test_transforms... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);

    auto root  = reg.create();
    auto child = reg.create();

    h.set_parent(child, root);

    using xfn = AffineTransform<double, 2>;
    xfn translate_5 = translation(Vec<double,2>{5.0, 0.0});
    xfn translate_3 = translation(Vec<double,2>{3.0, 0.0});

    xf.set_transform(root, translate_5);
    xf.set_transform(child, translate_3);

    // child's world transform should be translate_5 * translate_3
    xfn world = xf.object_to_world(child);
    Vec<double,2> origin {0.0, 0.0};
    Vec<double,2> result = world * origin;
    assert(std::abs(result[0] - 8.0) < 1e-10);
    assert(std::abs(result[1]) < 1e-10);

    std::cout << "ok\n";
}


void test_transform_traversal_functions() {
    std::cout << "test_transform_traversal_functions... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);

    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    auto c    = reg.create();

    h.set_parent(a, root);
    h.set_parent(b, root);
    h.set_parent(c, a);

    using V = Vec<double,2>;
    xf.set_transform(root, translation(V{100.0, 0.0}));
    xf.set_transform(a, translation(V{10.0, 0.0}));
    xf.set_transform(c, translation(V{1.0, 0.0}));

    std::vector<entt::entity> order;
    std::vector<double> x_to_root;
    for (auto g = walk::dfs(xf.traverse(root, SiblingOrder::Forward)); g; ++g) {
        order.push_back(g->node.node_id);
        x_to_root.push_back((g->node_to_root * V{0.0, 0.0})[0]);
    }

    assert((order == std::vector<entt::entity>{root, a, c, b}));

    // node_to_root is relative to traversal root, so root is identity.
    assert(std::abs(x_to_root[0] - 0.0) < 1e-10);
    assert(std::abs(x_to_root[1] - 10.0) < 1e-10);
    assert(std::abs(x_to_root[2] - 11.0) < 1e-10);
    assert(std::abs(x_to_root[3] - 0.0) < 1e-10);

    auto base = h.traverse(root, SiblingOrder::Forward);
    auto with_xf = xf.augment_with_transforms(
        base,
        [](const NodeEntry& n) { return n.node_id; }
    );
    std::vector<entt::entity> augmented_order;
    for (auto g = walk::dfs(with_xf); g; ++g) {
        augmented_order.push_back(g->node.node_id);
    }
    assert((augmented_order == order));

    std::cout << "ok\n";
}


// ============================================================
// Bounds system tests
// ============================================================

using R2 = Rect<double,2>;
using V2 = Vec<double,2>;

// helper: check that a parent's computed bounds contain a child's computed bounds
// (transformed into the parent's space)
void assert_parent_contains_child(
        BoundsSystem<SceneH, double, 2>& bs,
        TransformSystem<SceneH, double, 2>& xf,
        entt::entity parent,
        entt::entity child)
{
    auto parent_bounds = bs.get_computed_bounds(parent);
    auto child_bounds  = bs.get_computed_bounds(child);
    if (not child_bounds) return;  // child has no bounds, nothing to check

    assert(parent_bounds.has_value());

    // transform child bounds into parent space
    auto child_xf = xf.get_transform(child);
    R2 child_in_parent = *child_bounds;
    if (child_xf) {
        child_in_parent = ((*child_xf) * child_in_parent).bounds();
    }
    // parent must contain child
    assert(parent_bounds->contains(child_in_parent));
}


void test_bounds_basic() {
    std::cout << "test_bounds_basic... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    // set intrinsic bounds on child
    R2 child_rect {V2{0,0}, V2{10,10}};
    bs.set_intrinsic_bounds(child, child_rect);

    // child's computed bounds should equal its intrinsic bounds (no children of its own)
    auto cb = bs.get_computed_bounds(child);
    assert(cb.has_value());
    assert(*cb == child_rect);

    // root's computed bounds should propagate from child
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(*rb == child_rect);

    std::cout << "ok\n";
}


void test_bounds_union_of_children() {
    std::cout << "test_bounds_union_of_children... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    h.set_parent(a, root);
    h.set_parent(b, root);

    bs.set_intrinsic_bounds(a, R2{V2{0,0}, V2{5,5}});
    bs.set_intrinsic_bounds(b, R2{V2{10,10}, V2{20,20}});

    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    // union of [0,5] and [10,20] = [0,20]
    assert(rb->lo[0] == 0  && rb->lo[1] == 0);
    assert(rb->hi[0] == 20 && rb->hi[1] == 20);

    assert_parent_contains_child(bs, xf, root, a);
    assert_parent_contains_child(bs, xf, root, b);

    std::cout << "ok\n";
}


void test_bounds_with_transforms() {
    std::cout << "test_bounds_with_transforms... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    // child has a unit box at the origin
    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{1,1}});
    // child is translated by (100, 0) in parent space
    xf.set_transform(child, translation(V2{100.0, 0.0}));

    // root's computed bounds should be the child's bounds shifted by the transform
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 100.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 101.0) < 1e-10);

    assert_parent_contains_child(bs, xf, root, child);

    std::cout << "ok\n";
}


void test_bounds_dirty_after_reparent() {
    std::cout << "test_bounds_dirty_after_reparent... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto p1    = reg.create();
    auto p2    = reg.create();
    auto child = reg.create();
    h.set_parent(child, p1);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{10,10}});

    // p1 should have computed bounds from child
    auto b1 = bs.get_computed_bounds(p1);
    assert(b1.has_value());
    assert(*b1 == (R2{V2{0,0}, V2{10,10}}));

    // p2 should have no computed bounds
    assert(!bs.get_computed_bounds(p2).has_value());

    // reparent child to p2
    h.set_parent(child, p2);

    // p1 should now have no computed bounds (child moved away)
    auto b1_after = bs.get_computed_bounds(p1);
    assert(!b1_after.has_value());

    // p2 should now have computed bounds from child
    auto b2_after = bs.get_computed_bounds(p2);
    assert(b2_after.has_value());
    assert(*b2_after == (R2{V2{0,0}, V2{10,10}}));

    std::cout << "ok\n";
}


void test_bounds_dirty_after_unparent() {
    std::cout << "test_bounds_dirty_after_unparent... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{5,5}});

    assert(bs.get_computed_bounds(root).has_value());

    // remove child from hierarchy
    h.unparent(child);

    // root should have no bounds now
    assert(!bs.get_computed_bounds(root).has_value());

    std::cout << "ok\n";
}


void test_bounds_dirty_after_add_child() {
    std::cout << "test_bounds_dirty_after_add_child... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();

    bs.set_intrinsic_bounds(root,  R2{V2{0,0}, V2{1,1}});
    bs.set_intrinsic_bounds(child, R2{V2{100,100}, V2{200,200}});

    // root's computed bounds should just be its own intrinsic
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(*rb == (R2{V2{0,0}, V2{1,1}}));

    // now add child under root
    h.set_parent(child, root);

    // root's computed bounds should expand to include child
    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(rb->lo[0] == 0   && rb->lo[1] == 0);
    assert(rb->hi[0] == 200 && rb->hi[1] == 200);

    assert_parent_contains_child(bs, xf, root, child);
    
    std::cout << "ok\n";
}


void test_bounds_dirty_after_transform_change() {
    std::cout << "test_bounds_dirty_after_transform_change... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{1,1}});
    xf.set_transform(child, translation(V2{10.0, 0.0}));

    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 10.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 11.0) < 1e-10);

    // move child further away
    xf.set_transform(child, translation(V2{50.0, 0.0}));

    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 50.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 51.0) < 1e-10);

    assert_parent_contains_child(bs, xf, root, child);

    std::cout << "ok\n";
}

void test_bounds_dirty_after_transform_set() {
    std::cout << "test_bounds_dirty_after_transform_set... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{1,1}});

    // build and cache baseline without transform
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 0.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 1.0) < 1e-10);

    // setting an initial transform should dirty ancestors
    xf.set_transform(child, translation(V2{10.0, 0.0}));

    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 10.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 11.0) < 1e-10);

    std::cout << "ok\n";
}


void test_bounds_dirty_after_transform_remove() {
    std::cout << "test_bounds_dirty_after_transform_remove... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{1,1}});
    xf.set_transform(child, translation(V2{10.0, 0.0}));

    // build and cache transformed bounds
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 10.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 11.0) < 1e-10);

    // removing transform should dirty ancestors
    auto removed = xf.remove_transform(child);
    assert(removed.has_value());

    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 0.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 1.0) < 1e-10);

    std::cout << "ok\n";
}


void test_bounds_deep_hierarchy() {
    std::cout << "test_bounds_deep_hierarchy... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    //  root
    //   └─ a (translate 10,0)
    //       └─ b (translate 0,10)
    //           └─ c (bounds [0,1])
    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    auto c    = reg.create();

    h.set_parent(a, root);
    h.set_parent(b, a);
    h.set_parent(c, b);

    xf.set_transform(a, translation(V2{10.0, 0.0}));
    xf.set_transform(b, translation(V2{0.0, 10.0}));
    bs.set_intrinsic_bounds(c, R2{V2{0,0}, V2{1,1}});

    // c's computed bounds = [0,1]x[0,1]
    // b's computed bounds = [0,1]x[0,1] (just c, no transform on c)
    // a's computed bounds = b's in a's space = translate(0,10) * [0,1]x[0,1] = [0,1]x[10,11]
    // root's computed bounds = a's in root space = translate(10,0) * [0,1]x[10,11] = [10,11]x[10,11]
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0] - 10.0) < 1e-10);
    assert(std::abs(rb->hi[0] - 11.0) < 1e-10);
    assert(std::abs(rb->lo[1] - 10.0) < 1e-10);
    assert(std::abs(rb->hi[1] - 11.0) < 1e-10);

    // verify containment at each level
    assert_parent_contains_child(bs, xf, root, a);
    assert_parent_contains_child(bs, xf, a, b);
    assert_parent_contains_child(bs, xf, b, c);

    // now reparent c directly under root
    h.set_parent(c, root);

    // b should have no bounds now (no children, no intrinsic)
    assert(!bs.get_computed_bounds(b).has_value());

    // a should have no bounds now (b has no bounds)
    assert(!bs.get_computed_bounds(a).has_value());

    // root should have c's bounds directly (no transform on c after reparent)
    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(std::abs(rb->lo[0]) < 1e-10);
    assert(std::abs(rb->hi[0] - 1.0) < 1e-10);

    assert_parent_contains_child(bs, xf, root, c);

    std::cout << "ok\n";
}


void test_bounds_intrinsic_change() {
    std::cout << "test_bounds_intrinsic_change... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    bs.set_intrinsic_bounds(child, R2{V2{0,0}, V2{5,5}});

    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(rb->hi[0] == 5 && rb->hi[1] == 5);

    // expand child's bounds
    bs.set_intrinsic_bounds(child, R2{V2{-10,-10}, V2{50,50}});

    rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(rb->lo[0] == -10 && rb->lo[1] == -10);
    assert(rb->hi[0] == 50  && rb->hi[1] == 50);

    // remove child's intrinsic bounds
    bs.remove_intrinsic_bounds(child);

    rb = bs.get_computed_bounds(root);
    assert(!rb.has_value());

    std::cout << "ok\n";
}


void test_bounds_parent_and_child_intrinsic() {
    std::cout << "test_bounds_parent_and_child_intrinsic... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root  = reg.create();
    auto child = reg.create();
    h.set_parent(child, root);

    // both parent and child have intrinsic bounds
    bs.set_intrinsic_bounds(root,  R2{V2{0,0}, V2{1,1}});
    bs.set_intrinsic_bounds(child, R2{V2{100,100}, V2{200,200}});

    // root's computed bounds should be union of its intrinsic and child's
    auto rb = bs.get_computed_bounds(root);
    assert(rb.has_value());
    assert(rb->lo[0] == 0   && rb->lo[1] == 0);
    assert(rb->hi[0] == 200 && rb->hi[1] == 200);

    assert_parent_contains_child(bs, xf, root, child);

    std::cout << "ok\n";
}


void test_bounds_traversal_functions() {
    std::cout << "test_bounds_traversal_functions... ";

    entt::registry reg;
    HierarchySystem<SceneH> h(reg);
    TransformSystem<SceneH, double, 2> xf(reg, h);
    BoundsSystem<SceneH, double, 2> bs(reg, xf);

    auto root = reg.create();
    auto a    = reg.create();
    auto b    = reg.create();
    h.set_parent(a, root);
    h.set_parent(b, root);

    xf.set_transform(a, translation(V2{5.0, 0.0}));
    xf.set_transform(b, translation(V2{-5.0, 10.0}));

    bs.set_intrinsic_bounds(a, R2{V2{0,0}, V2{2,2}});
    bs.set_intrinsic_bounds(b, R2{V2{0,0}, V2{2,2}});

    std::vector<entt::entity> full;
    for (auto g = walk::dfs(bs.traverse(root, SiblingOrder::Forward)); g; ++g) {
        full.push_back(g->node.node_id);
    }
    assert((full == std::vector<entt::entity>{root, a, b}));

    V2 point{5.5, 1.0};
    std::vector<entt::entity> under_point;
    for (auto g = walk::dfs(bs.traverse_under_point(bs.traverse(root, SiblingOrder::Forward), point)); g; ++g) {
        under_point.push_back(g->node.node_id);
    }
    assert((under_point == std::vector<entt::entity>{root, a}));

    std::vector<entt::entity> search_point;
    for (auto g = bs.search_under_point(root, SiblingOrder::Forward, DfsOrder::ShallowFirst, point); g; ++g) {
        search_point.push_back(g->node.node_id);
    }
    assert((search_point == std::vector<entt::entity>{a}));

    Ray<double,2> ray{V2{0.0, 1.0}, V2{1.0, 0.0}};
    std::vector<entt::entity> along_ray;
    for (auto g = walk::dfs(bs.traverse_along_ray(bs.traverse(root, SiblingOrder::Forward), ray)); g; ++g) {
        along_ray.push_back(g->node.node_id);
    }
    assert((along_ray == std::vector<entt::entity>{root, a}));

    std::vector<entt::entity> search_ray;
    for (auto g = bs.search_along_ray(root, SiblingOrder::Forward, DfsOrder::ShallowFirst, ray); g; ++g) {
        search_ray.push_back(g->node.node_id);
    }
    assert((search_ray == std::vector<entt::entity>{root, a}));

    std::cout << "ok\n";
}


int main() {
    test_basic_hierarchy();
    test_multiple_children_ordering();
    test_reparenting();
    test_unparent();
    test_signals();
    test_traversal();
    test_traversal_adaptors_and_walkers();
    test_dca_and_path();
    test_multiple_hierarchies();
    test_default_template_params();
    test_multiple_transform_layers_on_one_hierarchy();
    test_multiple_bounds_layers_on_one_hierarchy();
    test_separate_hierarchies_are_independent_with_layers();
    test_transforms();
    test_transform_traversal_functions();

    // bounds system tests
    test_bounds_basic();
    test_bounds_union_of_children();
    test_bounds_with_transforms();
    test_bounds_dirty_after_reparent();
    test_bounds_dirty_after_unparent();
    test_bounds_dirty_after_add_child();
    test_bounds_dirty_after_transform_change();
    test_bounds_dirty_after_transform_set();
    test_bounds_dirty_after_transform_remove();
    test_bounds_deep_hierarchy();
    test_bounds_intrinsic_change();
    test_bounds_parent_and_child_intrinsic();
    test_bounds_traversal_functions();

    std::cout << "\nall tests passed.\n";
    return 0;
}
