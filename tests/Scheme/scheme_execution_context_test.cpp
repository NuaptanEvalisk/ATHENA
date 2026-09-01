/******************************************************************************
* MODULE     : scheme_execution_context_test.cpp
* DESCRIPTION: Thread isolation for Scheme execution ownership
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "drd_std.hpp"
#include "scheme_execution_context.hpp"

#include <atomic>
#include <thread>

class TestSchemeExecutionContext: public QObject {
  Q_OBJECT

private slots:
  void restoresNestedContexts ();
  void isolatesConcurrentThreads ();
};

void
TestSchemeExecutionContext::restoresNestedContexts () {
  drd_info outer_drd ("execution-context-outer");
  drd_info inner_drd ("execution-context-inner");
  SchemeExecutionContext outer (
    nullptr, nullptr, &outer_drd, url_none (), url_none (), 1,
    SCHEME_CAPABILITY_GLOBAL);
  SchemeExecutionContext inner (
    nullptr, nullptr, &inner_drd, url_none (), url_none (), 2,
    SCHEME_CAPABILITY_GLOBAL);

  QVERIFY (current_scheme_execution_context () == nullptr);
  {
    SchemeExecutionScope outer_scope (outer);
    QCOMPARE (current_scheme_execution_context (), &outer);
    QCOMPARE (&current_drd (), &outer_drd);
    {
      SchemeExecutionScope inner_scope (inner);
      QCOMPARE (current_scheme_execution_context (), &inner);
      QCOMPARE (&current_drd (), &inner_drd);
    }
    QCOMPARE (current_scheme_execution_context (), &outer);
    QCOMPARE (&current_drd (), &outer_drd);
  }
  QVERIFY (current_scheme_execution_context () == nullptr);
  QCOMPARE (&current_drd (), &std_drd);
}

void
TestSchemeExecutionContext::isolatesConcurrentThreads () {
  std::atomic<int> ready (0);
  std::atomic<bool> release (false);
  std::atomic<bool> valid (true);

  auto worker= [&] (std::uint64_t command_id) {
    drd_info local_drd ("execution-context-worker");
    SchemeExecutionContext context (
      nullptr, nullptr, &local_drd, url_none (), url_none (), command_id,
      SCHEME_CAPABILITY_GLOBAL);
    SchemeExecutionScope scope (context);
    ready.fetch_add (1, std::memory_order_release);
    while (!release.load (std::memory_order_acquire)) std::this_thread::yield ();
    if (current_scheme_execution_context () != &context ||
        current_scheme_execution_context ()->command_id != command_id ||
        &current_drd () != &local_drd)
      valid.store (false, std::memory_order_relaxed);
  };

  std::thread first (worker, 11);
  std::thread second (worker, 22);
  while (ready.load (std::memory_order_acquire) != 2) std::this_thread::yield ();
  release.store (true, std::memory_order_release);
  first.join ();
  second.join ();

  QVERIFY (valid.load (std::memory_order_relaxed));
  QVERIFY (current_scheme_execution_context () == nullptr);
}

QTEST_MAIN (TestSchemeExecutionContext)
#include "scheme_execution_context_test.moc"
