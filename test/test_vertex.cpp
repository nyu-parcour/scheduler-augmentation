#include "gtest/gtest.h"

#include <atomic>
#include <type_traits>

#include <parlay/parallel.h>
#include <parlay/vertices/dynamic_vertex.h>

#include "test_vertex_types.h"

// Tests that install a second vertex type only make sense when the scheduler
// dispatches vertex types at runtime.
constexpr bool dynamic_scheduler =
    std::is_same_v<parlay::internal::scheduler_vertex_type, parlay::dynamic_vertex>;

// Binary fork tree of the given depth: 2^depth - 1 forks.
void fork_tree(int depth) {
  if (depth == 0) return;
  parlay::par_do([&]() { fork_tree(depth - 1); }, [&]() { fork_tree(depth - 1); });
}

TEST(TestVertex, NullOutsideRegion) {
  EXPECT_EQ(parlay::current_vertex<tagged_vertex>(), nullptr);
}

TEST(TestVertex, VisibleInsideRegion) {
  tagged_vertex init;
  init.tag = 7;
  bool seen = false;
  auto v = parlay::augment(init, [&]() {
    tagged_vertex* cur = parlay::current_vertex<tagged_vertex>();
    if (!parlay::augmentation_enabled) {
      EXPECT_EQ(cur, nullptr);
      return;
    }
    ASSERT_NE(cur, nullptr);
    EXPECT_EQ(cur->tag, 7);
    seen = true;
  });
  EXPECT_EQ(v.tag, 7);
  EXPECT_EQ(seen, parlay::augmentation_enabled);
  EXPECT_EQ(parlay::current_vertex<tagged_vertex>(), nullptr);
}

TEST(TestVertex, ForkCountMatchesDag) {
  auto v = parlay::augment(tagged_vertex{}, [&]() { fork_tree(6); });
  if (parlay::augmentation_enabled) {
    EXPECT_EQ(v.forks, 63u);
  } else {
    EXPECT_EQ(v.forks, 0u);
  }
}

TEST(TestVertex, ChildStrandsHaveOwnVertex) {
  if (!parlay::augmentation_enabled) GTEST_SKIP();
  tagged_vertex init;
  init.tag = 3;
  parlay::augment(init, [&]() {
    tagged_vertex* root = parlay::current_vertex<tagged_vertex>();
    ASSERT_NE(root, nullptr);
    std::atomic<int> ok{0};
    auto check_child = [&]() {
      tagged_vertex* cur = parlay::current_vertex<tagged_vertex>();
      if (cur != nullptr && cur != root && cur->tag == 3) ok.fetch_add(1);
    };
    parlay::par_do(check_child, check_child);
    EXPECT_EQ(ok.load(), 2);
    // After the join the continuing strand's vertex is the parent again.
    EXPECT_EQ(parlay::current_vertex<tagged_vertex>(), root);
    EXPECT_EQ(root->forks, 1u);
  });
}

TEST(TestVertex, WritesThroughAccessorReachResult) {
  constexpr size_t n = 100000;
  auto v = parlay::augment(tagged_vertex{}, [&]() {
    parlay::parallel_for(0, n, [&](size_t) {
      if (tagged_vertex* cur = parlay::current_vertex<tagged_vertex>()) cur->marks++;
    }, 1);
  });
  if (parlay::augmentation_enabled) {
    EXPECT_EQ(v.marks, n);
  } else {
    EXPECT_EQ(v.marks, 0u);
  }
}

// The bodies below are templates so that, in a statically-configured
// scheduler build, the branch installing a second vertex type is discarded
// rather than instantiated (which would fail augment's static_assert).
template <bool Dyn>
void mismatched_type_is_null() {
  if constexpr (Dyn) {
    parlay::augment(tagged_vertex{}, [&]() {
      EXPECT_NE(parlay::current_vertex<tagged_vertex>(), nullptr);
      EXPECT_EQ(parlay::current_vertex<other_vertex>(), nullptr);
    });
  }
}

TEST(TestVertex, MismatchedTypeIsNull) {
  mismatched_type_is_null<dynamic_scheduler>();
}

template <bool Dyn>
void nested_region_of_different_type() {
  if constexpr (Dyn) {
    auto outer = parlay::augment(tagged_vertex{}, [&]() {
      fork_tree(2);
      other_vertex init;
      init.x = 11;
      auto inner = parlay::augment(init, [&]() {
        EXPECT_EQ(parlay::current_vertex<tagged_vertex>(), nullptr);
        ASSERT_NE(parlay::current_vertex<other_vertex>(), nullptr);
        EXPECT_EQ(parlay::current_vertex<other_vertex>()->x, 11);
        fork_tree(3);  // not counted by the outer region
      });
      EXPECT_EQ(inner.x, 11);
      EXPECT_NE(parlay::current_vertex<tagged_vertex>(), nullptr);
      EXPECT_EQ(parlay::current_vertex<other_vertex>(), nullptr);
      fork_tree(2);
    });
    EXPECT_EQ(outer.forks, 6u);
  }
}

TEST(TestVertex, NestedRegionOfDifferentType) {
  nested_region_of_different_type<dynamic_scheduler>();
}

TEST(TestVertex, DynamicVertexGet) {
  parlay::dynamic_vertex inert;
  EXPECT_EQ(inert.get<tagged_vertex>(), nullptr);
  tagged_vertex init;
  init.tag = 5;
  parlay::dynamic_vertex h(init);
  ASSERT_NE(h.get<tagged_vertex>(), nullptr);
  EXPECT_EQ(h.get<tagged_vertex>()->tag, 5);
  EXPECT_EQ(h.get<other_vertex>(), nullptr);
  const parlay::dynamic_vertex& ch = h;
  EXPECT_EQ(ch.get<tagged_vertex>()->tag, 5);
  auto out = h.take<tagged_vertex>();
  EXPECT_EQ(out.tag, 5);
  EXPECT_EQ(h.get<tagged_vertex>(), nullptr);
}
