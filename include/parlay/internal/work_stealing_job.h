
#ifndef PARLAY_INTERNAL_WORK_STEALING_JOB_H_
#define PARLAY_INTERNAL_WORK_STEALING_JOB_H_

#include <cassert>

#include <atomic>
#include <thread>
#include <type_traits>    // IWYU pragma: keep

#include "../vertex.h"

namespace parlay {

// Jobs are thunks -- i.e., functions that take no arguments
// and return nothing. Could be a lambda, e.g. [] () {}.
//
// A job carries a pointer to the vertex of the strand it executes (see
// vertex.h), so augmentation state rides with the job across steals. When
// the vertex type is disabled, the pointer member is compiled out (empty
// base) and the job has the same layout as an unaugmented one.

namespace internal {

template <typename V, bool Enabled = V::enabled>
struct job_vertex_base {
  explicit job_vertex_base(V* v) : job_vertex(v) {}
  V* job_vertex;
};

template <typename V>
struct job_vertex_base<V, false> {
  explicit job_vertex_base(V*) {}
};

}  // namespace internal

template <typename V>
struct WorkStealingJob : private internal::job_vertex_base<V> {
  using vertex_type = V;

  explicit WorkStealingJob(V* v = nullptr) : internal::job_vertex_base<V>(v), done{false} { }
  virtual ~WorkStealingJob() = default;

  void operator()() {
    assert(done.load(std::memory_order_relaxed) == false);
    if constexpr (V::enabled) {
      // job_vertex is null for jobs spawned outside any augmented region.
      V* v = this->job_vertex;
      current_vertex<V>::ptr = v;
      if (v) v->start();
      execute();
      if (v) v->stop();
    } else {
      execute();
    }
    done.store(true, std::memory_order_release);
  }

  [[nodiscard]] bool finished() const noexcept {
    return done.load(std::memory_order_acquire);
  }

  void wait() const noexcept {
    while (!finished())
      std::this_thread::yield();
  }

 protected:
  virtual void execute() = 0;
  std::atomic<bool> done;
};

// Holds a type-specific reference to a callable object
template<typename V, typename F>
struct JobImpl : WorkStealingJob<V> {
  static_assert(std::is_invocable_v<F&>);
  explicit JobImpl(F& _f, V* v = nullptr) : WorkStealingJob<V>(v), f(_f) { }
  void execute() override {
    f();
  }
 private:
  F& f;
};

template<typename V, typename F>
JobImpl<V, F> make_job(F& f, V* v) { return JobImpl<V, F>(f, v); }

}

#endif  // PARLAY_INTERNAL_WORK_STEALING_JOB_H_
