
#ifndef PARLAY_INTERNAL_WORK_STEALING_JOB_H_
#define PARLAY_INTERNAL_WORK_STEALING_JOB_H_

#include <cassert>

#include <atomic>
#include <thread>
#include <type_traits>    // IWYU pragma: keep

#ifdef PARLAY_AUG
#include "vertex.h"
#endif


namespace parlay {

// Jobs are thunks -- i.e., functions that take no arguments
// and return nothing. Could be a lambda, e.g. [] () {}.

struct WorkStealingJob {
#ifdef PARLAY_AUG
  using Vertex = internal::vertex::Vertex;
#endif
  virtual ~WorkStealingJob() = default;
#ifdef PARLAY_AUG
  WorkStealingJob(Vertex *v) : done{false}, job_vertex(v) { }
  void operator()() {
    assert(done.load(std::memory_order_relaxed) == false);
    Vertex::current = job_vertex;
    job_vertex->start();
    execute();
    job_vertex->stop();
    done.store(true, std::memory_order_release);
  }
#else
  WorkStealingJob() : done{false} { }
  void operator()() {
    assert(done.load(std::memory_order_relaxed) == false);
    execute();
    done.store(true, std::memory_order_release);
  }
#endif
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
#ifdef PARLAY_AUG
  Vertex *job_vertex;
#endif
};

// Holds a type-specific reference to a callable object
template<typename F>
struct JobImpl : WorkStealingJob {
  static_assert(std::is_invocable_v<F&>);
#ifdef PARLAY_AUG
  explicit JobImpl(F& _f, internal::vertex::Vertex *v) : WorkStealingJob(v), f(_f) { }
#else
  explicit JobImpl(F& _f) : WorkStealingJob(), f(_f) { }
#endif
  void execute() override {
    f();
  }
 private:
  F& f;
};

template<typename F>
#ifdef PARLAY_AUG
  JobImpl<F> make_job(F& f, internal::vertex::Vertex *v) { return JobImpl(f, v); }
#else
  JobImpl<F> make_job(F& f) { return JobImpl(f); }
#endif
}

#endif  // PARLAY_INTERNAL_WORK_STEALING_JOB_H_
