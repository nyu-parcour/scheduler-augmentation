// Vertex concept for scheduler augmentation.
//
// A "vertex" observes the fork-join structure of a parallel computation.
// The scheduler is templated on a vertex type; the hooks below are invoked
// at the corresponding points of the computation DAG.
//
// A vertex type V must provide:
//
//   static constexpr bool enabled;
//       If false, every hook site in the scheduler is compiled out entirely
//       (if constexpr), guaranteeing zero overhead.
//
//   V();                          // default-constructible (fork children)
//   V(V&&); V& operator=(V&&);    // movable
//
//   void start();                 // resume accounting on this strand
//   void stop();                  // pause accounting on this strand
//   void fork(V* left, V* right); // called on the parent at a fork point;
//                                 // left/right are freshly default-constructed
//   void join(V* left, V* right, V* join_v);
//                                 // called on the parent at a join point: fold
//                                 // *this and the finished children into
//                                 // join_v (freshly default-constructed). The
//                                 // scheduler then calls join_v->start() and
//                                 // move-assigns join_v into the parent, which
//                                 // becomes the vertex of the continuing strand.
//
// The scheduler tracks the vertex of the currently-executing strand in
// current_vertex<V>::ptr, so vertex types do not declare any statics.
//
// A vertex is installed for the dynamic extent of a computation with
//   V result = parlay::augment(V{initial}, [&]() { ... });
// (see parallel.h). Outside of any augment region, no hooks run.

#ifndef PARLAY_VERTEX_H_
#define PARLAY_VERTEX_H_

namespace parlay {

// The default vertex: observes nothing. All hook sites in the scheduler
// are compiled out, so the generated code is identical to a scheduler
// without augmentation support.
struct noop_vertex {
  static constexpr bool enabled = false;
  void start() {}
  void stop() {}
  void fork(noop_vertex*, noop_vertex*) {}
  void join(noop_vertex*, noop_vertex*, noop_vertex*) {}
};

// Per-vertex-type thread-local pointer to the vertex of the strand that
// the current thread is executing (nullptr outside any augmented region).
// Constant-initialized, so access needs no TLS init guard.
template <typename V>
struct current_vertex {
  static inline thread_local V* ptr = nullptr;
};

}  // namespace parlay

#endif  // PARLAY_VERTEX_H_
