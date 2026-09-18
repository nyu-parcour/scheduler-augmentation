// space_alloc: explicitly accounted allocation for space_vertex.
//
// space_vertex reports its measures from allocate()/deallocate() calls, but
// nothing in the library makes them: the allocator layer knows nothing about
// vertices. This header is the missing counterpart, in two forms.
//
//   space_alloc<T>(n, init) / space_free(p, n)
//       A raw pair. Allocates n elements, initializes them with a serial
//       loop, and charges n*sizeof(T) -- the logical size, not whatever the
//       pool allocator consumed, which rounds up to a power of two. Each call
//       is exactly one node of the graph that space_vertex folds, and the
//       call site of space_free is where the memory stops being live, so its
//       placement relative to a fork is part of what gets measured.
//
//   space_sequence<T>
//       parlay::sequence<T> with an instrumented allocator, charged for
//       whatever the sequence asks for. Note that this is a real sequence and
//       behaves like one: it stores a size_t capacity alongside the elements,
//       so an n-element sequence costs n*sizeof(T) plus that word, and it
//       initializes its elements with a parallel_for, which forks once the
//       length exceeds parlay's granularity threshold of 1 + 8192/sizeof(T)
//       elements. Both show up in the measurements, correctly so -- they are
//       things the program really does. sequence::uninitialized(n) skips the
//       initialization entirely and so never forks.
//
// NOTE: this assumes the allocator layer itself is *not* instrumented. If
// hooks are ever added to pool_allocator or type_allocator, every allocation
// made through here would be counted twice.

#ifndef PARLAY_SPACE_ALLOC_H_
#define PARLAY_SPACE_ALLOC_H_

#include <cstddef>
#include <cstdint>

#include <atomic>
#include <memory>
#include <new>

#include "../alloc.h"
#include "../parallel.h"
#include "../sequence.h"

#include "space_vertex.h"

namespace parlay {

// The live total across all strands, and the largest value it has reached.
// These describe the run that actually happened, so unlike space_vertex's
// measures they depend on the schedule: they are what the P*R1 bound is a
// bound on, not another way of computing it.
//
// Both are maintained whether or not augmentation is enabled, so that a build
// with the vertex compiled out still reports a footprint.
namespace internal {
inline std::atomic<std::int64_t> space_live{0};
inline std::atomic<std::int64_t> space_high_water{0};
}  // namespace internal

// Bytes currently held by space_alloc allocations.
inline std::int64_t space_live_bytes() noexcept {
  return internal::space_live.load(std::memory_order_relaxed);
}

// The largest value space_live_bytes() has taken since the last reset.
inline std::int64_t space_high_water_bytes() noexcept {
  return internal::space_high_water.load(std::memory_order_relaxed);
}

// Zero both counters. Call before a measured region.
inline void space_reset_counters() noexcept {
  internal::space_live.store(0, std::memory_order_relaxed);
  internal::space_high_water.store(0, std::memory_order_relaxed);
}

// Charge n bytes to the space_vertex of the strand the calling thread is
// currently executing. A no-op outside an augmented space_vertex region, and
// compiled out entirely when augmentation is disabled.
//
// The vertex is looked up on every call and never cached: at a join the
// scheduler move-assigns the folded vertex over the parent's, so a pointer
// obtained before a par_do refers to an object that no longer exists.
inline void space_charge(std::size_t n) noexcept {
  if constexpr (augmentation_enabled) {
    if (space_vertex* v = current_vertex<space_vertex>()) v->allocate(n);
  }
  // The live total can only rise here, so this is the only place the peak
  // needs to be republished. Relaxed suffices: the counters are not used to
  // order anything, and the scheduler's joins already give the final reader
  // a happens-before edge to every strand that touched them.
  const std::int64_t live =
      internal::space_live.fetch_add(static_cast<std::int64_t>(n), std::memory_order_relaxed) +
      static_cast<std::int64_t>(n);
  std::int64_t peak = internal::space_high_water.load(std::memory_order_relaxed);
  while (peak < live && !internal::space_high_water.compare_exchange_weak(
                            peak, live, std::memory_order_relaxed)) {
  }
}

// Credit n bytes back to the current strand's vertex.
inline void space_credit(std::size_t n) noexcept {
  if constexpr (augmentation_enabled) {
    if (space_vertex* v = current_vertex<space_vertex>()) v->deallocate(n);
  }
  internal::space_live.fetch_sub(static_cast<std::int64_t>(n), std::memory_order_relaxed);
}

// Allocate n elements initialized to init, and charge n*sizeof(T) bytes.
template <typename T>
T* space_alloc(std::size_t n, const T& init = T{}) {
  T* p = allocator<T>{}.allocate(n);
  for (std::size_t i = 0; i < n; i++) ::new (static_cast<void*>(p + i)) T(init);
  space_charge(n * sizeof(T));
  return p;
}

// Free an n-element block from space_alloc, crediting n*sizeof(T) bytes.
// The call site is the point in the graph where the memory stops being live,
// so where it is placed relative to a par_do is part of what gets measured.
template <typename T>
void space_free(T* p, std::size_t n) {
  space_credit(n * sizeof(T));
  for (std::size_t i = 0; i < n; i++) p[i].~T();
  allocator<T>{}.deallocate(p, n);
}

// An allocator that charges the current strand's vertex for every allocation
// it serves, so that a container built on it is measured like space_alloc'd
// memory. What is charged is what the container asks for, which for a
// container that stores bookkeeping alongside its elements includes that
// bookkeeping.
template <typename T>
struct space_allocator {
  using value_type = T;

  T* allocate(std::size_t n) {
    space_charge(n * sizeof(T));
    return allocator<T>{}.allocate(n);
  }

  void deallocate(T* p, std::size_t n) {
    space_credit(n * sizeof(T));
    allocator<T>{}.deallocate(p, n);
  }

  constexpr space_allocator() = default;
  template <typename U>
  constexpr space_allocator(const space_allocator<U>&) noexcept {}
};

template <typename T, typename U>
constexpr bool operator==(const space_allocator<T>&, const space_allocator<U>&) noexcept {
  return true;
}
template <typename T, typename U>
constexpr bool operator!=(const space_allocator<T>&, const space_allocator<U>&) noexcept {
  return false;
}

// A parlay::sequence whose buffers are charged to the current strand's
// space_vertex. Note that sequence prepends a size_t capacity to each buffer
// it allocates, so an n-element sequence costs n*sizeof(T) plus that word.
template <typename T>
using space_sequence = sequence<T, space_allocator<T>>;

}  // namespace parlay

#endif  // PARLAY_SPACE_ALLOC_H_
