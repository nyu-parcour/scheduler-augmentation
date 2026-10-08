// Detects a worker running unrelated work on top of a waiting frame.
// Each frame at tree depth d increments a thread-local "active frames" count.
// Without stacking, the active frames on a thread are a chain of ancestors, so count <= d+1.
#include <parlay/parallel.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
thread_local int active = 0;
std::atomic<long> events{0}, max_excess{0};
bool cons;
void rec(int d, int maxd) {
  active++;
  if (active > d + 1) { events++; long e = active - (d + 1); long m = max_excess; while (e > m && !max_excess.compare_exchange_weak(m, e)); }
  if (d == maxd) { volatile long s = 0; for (int i = 0; i < 2000 + (rand() % 2000); i++) s += i; }
  else parlay::par_do([&]{ rec(d + 1, maxd); }, [&]{ rec(d + 1, maxd); }, cons);
  active--;
}
int main(int argc, char** argv) {
  cons = argc > 1 && !strcmp(argv[1], "conservative");
  rec(0, 14);
  printf("conservative=%d stacking_events=%ld max_extra_frames=%ld\n", cons, events.load(), max_excess.load());
}
