
/******************************************************************************
* MODULE     : timer.cpp
* DESCRIPTION: timers
* COPYRIGHT  : (C) 1999  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_timer.hpp"
#include "iterator.hpp"
#include "merge_sort.hpp"

namespace {

struct timing_state {
  hashmap<string,int> level;
  hashmap<string,int> nr;
  hashmap<string,int> cumul;
  hashmap<string,int> last;

  timing_state (): level (0), nr (0), cumul (0), last (0) {}
};

// Benchmark scopes are entered and left by the same execution context.  Keeping
// their mutable bookkeeping local to that thread avoids sharing the legacy
// hashmap implementation between BufferActors and the Qt thread.
thread_local timing_state timing;

} // namespace

/******************************************************************************
* Getting the time
******************************************************************************/

time_t
raw_time () {
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tp;
  gettimeofday (&tp, NULL);
  return (time_t) ((tp.tv_sec * 1000) + (tp.tv_usec / 1000));
#else
  timeb tb;
  ftime (&tb);
  return (time_t) ((tb.time * 1000) + tb.millitm);
#endif
}

static time_t start_time= raw_time ();

time_t
texmacs_time () {
#ifdef HAVE_GETTIMEOFDAY
  struct timeval tp;
  gettimeofday (&tp, NULL);
  return ((time_t) ((tp.tv_sec * 1000) + (tp.tv_usec / 1000))) - start_time;
#else
  timeb tb;
  ftime (&tb);
  return ((time_t) ((tb.time * 1000) + tb.millitm)) - start_time;
#endif
}

/******************************************************************************
* Routines for benchmarking
******************************************************************************/

void
bench_start (string task) {
  // start timer for a given type of task
  if (timing.level [task] == 0)
    timing.last (task)= (int) texmacs_time ();
  timing.level (task) ++;
}

void
bench_cumul (string task) {
  // end timer for a given type of task, but don't reset timer
  timing.level (task) --;
  if (timing.level [task] == 0) {
    int ms= ((int) texmacs_time ()) - timing.last (task);
    timing.nr    (task) ++;
    timing.cumul (task) += ms;
    timing.last -> reset (task);
  }
}

void
bench_end (string task) {
  // end timer for a given type of task, print result and reset timer
  bench_cumul (task);
  bench_print (task);
  bench_reset (task);
}

void
bench_reset (string task) {
  // reset timer for a given type of task
  timing.level->reset (task);
  timing.nr   ->reset (task);
  timing.cumul->reset (task);
  timing.last ->reset (task);
}

void
bench_print (string task) {
  // print timing for a given type of task
  if (DEBUG_BENCH) {
    int nr= timing.nr [task];
    std_bench << "Task '" << task << "' took "
              << timing.cumul [task] << " ms";
    if (nr > 1) std_bench << " (" << nr << " invocations)";
    std_bench << "\n";
  }
}

static array<string>
collect (hashmap<string,int> h) {
  array<string> a;
  iterator<string> it= iterate (h);
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  return a;
}

void
bench_print () {
  // print timings for all types of tasks
  array<string> a= collect (timing.cumul);
  int i, n= N(a);
  for (i=0; i<n; i++)
    bench_print (a[i]);
}
