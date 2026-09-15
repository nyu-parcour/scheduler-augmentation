// Vertex types used by test_vertex.cpp. Kept in a header so the test can also
// be built with -include test/test_vertex_types.h -DPARLAY_VERTEX_TYPE=tagged_vertex
// to exercise a statically-configured scheduler.
#ifndef PARLAY_TEST_VERTEX_TYPES_H_
#define PARLAY_TEST_VERTEX_TYPES_H_

// A vertex that counts forks, propagates a tag from parent to children at
// forks, and sums a per-strand "marks" counter at joins.
struct tagged_vertex {
  static constexpr bool enabled = true;
  unsigned long long forks = 0;
  unsigned long long marks = 0;
  int tag = 0;

  void start() {}
  void stop() {}
  void fork(tagged_vertex* l, tagged_vertex* r) {
    forks++;
    l->tag = tag;
    r->tag = tag;
  }
  void join(tagged_vertex* l, tagged_vertex* r, tagged_vertex* j) {
    j->forks = forks + l->forks + r->forks;
    j->marks = marks + l->marks + r->marks;
    j->tag = tag;
  }
};

struct other_vertex {
  static constexpr bool enabled = true;
  int x = 0;
  void start() {}
  void stop() {}
  void fork(other_vertex*, other_vertex*) {}
  void join(other_vertex*, other_vertex*, other_vertex* j) { j->x = x; }
};

#endif  // PARLAY_TEST_VERTEX_TYPES_H_
