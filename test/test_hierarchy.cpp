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

    std::cout << "\nall tests passed.\n";
    return 0;
}
