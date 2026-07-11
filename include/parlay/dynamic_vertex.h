// dynamic_vertex: a type-erased vertex (see vertex.h for the vertex concept).
//
// A scheduler instantiated with dynamic_vertex supports runtime augmentation:
//   V result = parlay::augment(V{initial}, [&]() { ... });
// installs the concrete vertex type V for the dynamic extent of the region,
// and the scheduler dispatches the hooks through a per-type table of function
// pointers. Different (possibly nested or concurrent) regions may use
// different concrete vertex types.
//
// Outside any augmented region a dynamic_vertex is inert (ops == nullptr) and
// every hook degrades to a single predictable branch.
//
// Concrete vertex types are stored inline, so they must fit in
// PARLAY_DYNAMIC_VERTEX_CAPACITY bytes (default 48, which keeps a
// dynamic_vertex to exactly one cache line). Define the macro to raise the
// limit for larger vertex types; like the vertex selection itself, every
// translation unit of a program must agree on the value.

#ifndef PARLAY_DYNAMIC_VERTEX_H_
#define PARLAY_DYNAMIC_VERTEX_H_

#ifndef PARLAY_DYNAMIC_VERTEX_CAPACITY
#define PARLAY_DYNAMIC_VERTEX_CAPACITY 48
#endif

#include <cassert>
#include <cstddef>

#include <new>
#include <type_traits>
#include <utility>

namespace parlay {

// Type-erased hook table. One instance exists per concrete vertex type
// (see vertex_ops_for below); a null table pointer means "no augmentation".
struct vertex_ops {
  void (*start)(void*);
  void (*stop)(void*);
  // Placement-constructs fresh children into left_buf/right_buf, then calls
  // parent->fork(left, right).
  void (*fork)(void* parent, void* left_buf, void* right_buf);
  // Calls parent->join(left, right), then destroys left and right.
  void (*join)(void* parent, void* left, void* right);
  void (*move_construct)(void* dst, void* src);
  void (*destroy)(void*);
};

template <typename V>
inline constexpr vertex_ops vertex_ops_for = {
    [](void* p) { static_cast<V*>(p)->start(); },
    [](void* p) { static_cast<V*>(p)->stop(); },
    [](void* p, void* l, void* r) {
      V* lp = ::new (l) V();
      V* rp = ::new (r) V();
      static_cast<V*>(p)->fork(lp, rp);
    },
    [](void* p, void* l, void* r) {
      V* lp = static_cast<V*>(l);
      V* rp = static_cast<V*>(r);
      static_cast<V*>(p)->join(lp, rp);
      lp->~V();
      rp->~V();
    },
    [](void* dst, void* src) { ::new (dst) V(std::move(*static_cast<V*>(src))); },
    [](void* p) { static_cast<V*>(p)->~V(); },
};

// The ops pointer plus inline storage for the concrete vertex; with the
// default capacity this occupies exactly one cache line. The full-line
// alignment also keeps the left/right child vertices in pardo's stack frame
// on separate cache lines (the right child may be written by a thief).
class alignas(64) dynamic_vertex {
 public:
  static constexpr bool enabled = true;
  static constexpr std::size_t payload_capacity = PARLAY_DYNAMIC_VERTEX_CAPACITY;
  static_assert(payload_capacity > 0, "PARLAY_DYNAMIC_VERTEX_CAPACITY must be positive");

  dynamic_vertex() noexcept : ops(nullptr) {}

  template <typename V,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<V>, dynamic_vertex>>>
  explicit dynamic_vertex(V&& v) : ops(&vertex_ops_for<std::decay_t<V>>) {
    using U = std::decay_t<V>;
    static_assert(U::enabled, "augmenting with a disabled vertex type is pointless");
    static_assert(sizeof(U) <= payload_capacity,
                  "vertex type too large for dynamic_vertex payload "
                  "(define PARLAY_DYNAMIC_VERTEX_CAPACITY to raise the limit)");
    static_assert(alignof(U) <= alignof(std::max_align_t), "vertex type over-aligned for dynamic_vertex payload");
    ::new (static_cast<void*>(payload)) U(std::forward<V>(v));
  }

  dynamic_vertex(dynamic_vertex&& other) noexcept : ops(other.ops) {
    if (ops) {
      ops->move_construct(payload, other.payload);
      other.reset();
    }
  }

  dynamic_vertex& operator=(dynamic_vertex&& other) noexcept {
    if (this != &other) {
      if (ops) ops->destroy(payload);
      ops = other.ops;
      if (ops) {
        ops->move_construct(payload, other.payload);
        other.reset();
      }
    }
    return *this;
  }

  dynamic_vertex(const dynamic_vertex&) = delete;
  dynamic_vertex& operator=(const dynamic_vertex&) = delete;

  ~dynamic_vertex() {
    if (ops) ops->destroy(payload);
  }

  void start() {
    if (ops) ops->start(payload);
  }

  void stop() {
    if (ops) ops->stop(payload);
  }

  void fork(dynamic_vertex* left, dynamic_vertex* right) {
    // Children inherit the region's concrete vertex type.
    left->ops = ops;
    right->ops = ops;
    if (ops) ops->fork(payload, left->payload, right->payload);
  }

  void join(dynamic_vertex* left, dynamic_vertex* right) {
    if (ops) ops->join(payload, left->payload, right->payload);
    // The join op already destroyed the children's payloads.
    left->ops = nullptr;
    right->ops = nullptr;
  }

  // Extracts the concrete vertex, leaving this handle inert.
  // V must be the type this handle was constructed from.
  template <typename V>
  V take() {
    assert(ops == &vertex_ops_for<V>);
    V* p = std::launder(reinterpret_cast<V*>(payload));
    V out = std::move(*p);
    ops->destroy(payload);
    ops = nullptr;
    return out;
  }

 private:
  void reset() noexcept {
    ops->destroy(payload);
    ops = nullptr;
  }

  const vertex_ops* ops;
  alignas(std::max_align_t) unsigned char payload[payload_capacity];
};

// One cache line at the default capacity; always a whole number of lines.
static_assert(sizeof(dynamic_vertex) % 64 == 0);
static_assert(PARLAY_DYNAMIC_VERTEX_CAPACITY != 48 || sizeof(dynamic_vertex) == 64);

}  // namespace parlay

#endif  // PARLAY_DYNAMIC_VERTEX_H_
