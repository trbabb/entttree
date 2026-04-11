#include <cassert>
#include <iostream>
#include <vector>

#include <theta-hierarchy/bounding_hierarchy.h>

using namespace theta;

// hierarchy tags
struct SceneH {};
struct CollisionH {};


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
    for (auto g = h.children(root, SiblingTraversalOrder::Forward); g; ++g) {
        kids.push_back(g->node_id);
    }
    assert(kids.size() == 3);
    assert(kids[0] == a);
    assert(kids[1] == b);
    assert(kids[2] == c);

    // reorder: move c before a
    h.order_child_before(c, a);
    kids.clear();
    for (auto g = h.children(root, SiblingTraversalOrder::Forward); g; ++g) {
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

    int added_count = 0;
    int changed_count = 0;
    int removed_count = 0;

    h.on_added.connect([&](entt::entity, ParentConnection<SceneH>) {
        ++added_count;
    });
    h.on_changed.connect([&](entt::entity, ParentConnection<SceneH>, ParentConnection<SceneH>) {
        ++changed_count;
    });
    h.on_removed.connect([&](entt::entity, ParentConnection<SceneH>) {
        ++removed_count;
    });

    auto root = reg.create();
    auto child = reg.create();
    auto other = reg.create();

    h.set_parent(child, root);
    assert(added_count == 1);

    h.set_parent(child, other);  // reparent
    assert(changed_count == 1);

    h.unparent(child);
    assert(removed_count == 1);

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
    for (auto g = h.traverse_dfs(root); g; ++g) {
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


int main() {
    test_basic_hierarchy();
    test_multiple_children_ordering();
    test_reparenting();
    test_unparent();
    test_signals();
    test_traversal();
    test_dca_and_path();
    test_multiple_hierarchies();
    test_transforms();

    // bounds system tests
    test_bounds_basic();
    test_bounds_union_of_children();
    test_bounds_with_transforms();
    test_bounds_dirty_after_reparent();
    test_bounds_dirty_after_unparent();
    test_bounds_dirty_after_add_child();
    test_bounds_dirty_after_transform_change();
    test_bounds_deep_hierarchy();
    test_bounds_intrinsic_change();
    test_bounds_parent_and_child_intrinsic();

    std::cout << "\nall tests passed.\n";
    return 0;
}
