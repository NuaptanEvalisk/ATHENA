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
#include "System/Misc/crash_report.hpp"

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
  tree outer_document= make_document_tree ();
  tree inner_document= make_document_tree ();
  drd_info* default_drd= &current_drd ();
  tree* default_document= &current_document_tree ();
  SchemeExecutionContext outer (
    nullptr, nullptr, &outer_drd, &outer_document,
    ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 1,
    SCHEME_CAPABILITY_GLOBAL);
  SchemeExecutionContext inner (
    nullptr, nullptr, &inner_drd, &inner_document,
    ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 2,
    SCHEME_CAPABILITY_GLOBAL);

  QVERIFY (current_scheme_execution_context () == nullptr);
  {
    SchemeExecutionScope outer_scope (outer);
    QCOMPARE (current_scheme_execution_context (), &outer);
    QVERIFY (athena_crash_execution_report ().find ("command=1\n") != std::string::npos);
    QCOMPARE (&current_drd (), &outer_drd);
    QCOMPARE (&current_document_tree (), &outer_document);
    assign (path (0), tree (DOCUMENT, "outer"));
    {
      SchemeExecutionScope inner_scope (inner);
      QCOMPARE (current_scheme_execution_context (), &inner);
      QVERIFY (athena_crash_execution_report ().find ("command=2\n") != std::string::npos);
      QCOMPARE (&current_drd (), &inner_drd);
      QCOMPARE (&current_document_tree (), &inner_document);
      assign (path (0), tree (DOCUMENT, "inner"));
      QCOMPARE (subtree (inner_document, path (0)),
                tree (DOCUMENT, "inner"));
    }
    QCOMPARE (current_scheme_execution_context (), &outer);
    QVERIFY (athena_crash_execution_report ().find ("command=1\n") != std::string::npos);
    QCOMPARE (&current_drd (), &outer_drd);
    QCOMPARE (&current_document_tree (), &outer_document);
    QCOMPARE (subtree (outer_document, path (0)),
              tree (DOCUMENT, "outer"));
  }
  QVERIFY (current_scheme_execution_context () == nullptr);
  QVERIFY (athena_crash_execution_report ().find ("command=0\n") != std::string::npos);
  QCOMPARE (&current_drd (), default_drd);
  QCOMPARE (&current_document_tree (), default_document);
}

void
TestSchemeExecutionContext::isolatesConcurrentThreads () {
  std::atomic<int> ready (0);
  std::atomic<bool> release (false);
  std::atomic<bool> valid (true);

  auto worker= [&] (std::uint64_t command_id) {
    drd_info local_drd ("execution-context-worker");
    tree local_document= make_document_tree ();
    SchemeExecutionContext context (
      nullptr, nullptr, &local_drd, &local_document,
      ATHENA_NO_ACTOR, ATHENA_NO_VIEW,
      command_id,
      SCHEME_CAPABILITY_GLOBAL);
    SchemeExecutionScope scope (context);
    the_exception= as_string ((int) command_id);
    ready.fetch_add (1, std::memory_order_release);
    while (!release.load (std::memory_order_acquire)) std::this_thread::yield ();
    if (current_scheme_execution_context () != &context ||
        current_scheme_execution_context ()->command_id != command_id ||
        &current_drd () != &local_drd ||
        &current_document_tree () != &local_document ||
        the_exception != as_string ((int) command_id) ||
        athena_crash_execution_report ().find (
          "command=" + std::to_string (command_id) + "\n") == std::string::npos)
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
