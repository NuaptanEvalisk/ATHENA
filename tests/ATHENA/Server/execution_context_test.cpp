/******************************************************************************
* MODULE     : execution_context_test.cpp
* DESCRIPTION: Thread isolation for native execution ownership
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "convert.hpp"
#include "Data/NodeContracts/execution_state.hpp"
#include "execution_context.hpp"
#include "System/Misc/crash_report.hpp"

#include <atomic>
#include <thread>

bool headless_mode= true;
bool is_headless () { return true; }

tree latex_expand (tree doc);

class TestExecutionContext: public QObject {
  Q_OBJECT

private slots:
  void restoresNestedContexts ();
  void isolatesConcurrentThreads ();
  void latexExpansionAvoidsGuiRegistry ();
};

void
TestExecutionContext::restoresNestedContexts () {
  athena::contracts::evaluation_state outer_state;
  athena::contracts::evaluation_state inner_state;
  tree outer_document= make_document_tree ();
  tree inner_document= make_document_tree ();
  athena::contracts::evaluation_state* default_state= &athena::contracts::current_evaluation_state ();
  tree* default_document= &current_document_tree ();
  ExecutionContext outer (
    nullptr, nullptr, &outer_state, &outer_document,
    ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 1,
    EXECUTION_CAPABILITY_GLOBAL);
  ExecutionContext inner (
    nullptr, nullptr, &inner_state, &inner_document,
    ATHENA_NO_ACTOR, ATHENA_NO_VIEW, 2,
    EXECUTION_CAPABILITY_GLOBAL);

  QVERIFY (current_execution_context () == nullptr);
  {
    ExecutionScope outer_scope (outer);
    QCOMPARE (current_execution_context (), &outer);
    QVERIFY (athena_crash_execution_report ().find ("command=1\n") != std::string::npos);
    QCOMPARE (&athena::contracts::current_evaluation_state (), &outer_state);
    QCOMPARE (&current_document_tree (), &outer_document);
    assign (path (0), tree (DOCUMENT, "outer"));
    {
      ExecutionScope inner_scope (inner);
      QCOMPARE (current_execution_context (), &inner);
      QVERIFY (athena_crash_execution_report ().find ("command=2\n") != std::string::npos);
      QCOMPARE (&athena::contracts::current_evaluation_state (), &inner_state);
      QCOMPARE (&current_document_tree (), &inner_document);
      assign (path (0), tree (DOCUMENT, "inner"));
      QCOMPARE (subtree (inner_document, path (0)),
                tree (DOCUMENT, "inner"));
    }
    QCOMPARE (current_execution_context (), &outer);
    QVERIFY (athena_crash_execution_report ().find ("command=1\n") != std::string::npos);
    QCOMPARE (&athena::contracts::current_evaluation_state (), &outer_state);
    QCOMPARE (&current_document_tree (), &outer_document);
    QCOMPARE (subtree (outer_document, path (0)),
              tree (DOCUMENT, "outer"));
  }
  QVERIFY (current_execution_context () == nullptr);
  QVERIFY (athena_crash_execution_report ().find ("command=0\n") != std::string::npos);
  QCOMPARE (&athena::contracts::current_evaluation_state (), default_state);
  QCOMPARE (&current_document_tree (), default_document);
}

void
TestExecutionContext::isolatesConcurrentThreads () {
  std::atomic<int> ready (0);
  std::atomic<bool> release (false);
  std::atomic<bool> valid (true);

  auto worker= [&] (std::uint64_t command_id) {
    athena::contracts::evaluation_state local_state;
    tree local_document= make_document_tree ();
    ExecutionContext context (
      nullptr, nullptr, &local_state, &local_document,
      ATHENA_NO_ACTOR, ATHENA_NO_VIEW,
      command_id,
      EXECUTION_CAPABILITY_GLOBAL);
    ExecutionScope scope (context);
    the_exception= as_string ((int) command_id);
    ready.fetch_add (1, std::memory_order_release);
    while (!release.load (std::memory_order_acquire)) std::this_thread::yield ();
    if (current_execution_context () != &context ||
        current_execution_context ()->command_id != command_id ||
        &athena::contracts::current_evaluation_state () != &local_state ||
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
  QVERIFY (current_execution_context () == nullptr);
}

void
TestExecutionContext::latexExpansionAvoidsGuiRegistry () {
  athena::contracts::evaluation_state local_state;
  tree local_document= make_document_tree ();
  ExecutionContext context (
    nullptr, nullptr, &local_state, &local_document,
    ATHENA_NO_ACTOR, 7, 77, EXECUTION_CAPABILITY_BUFFER);

  tree doc (TUPLE);
  doc << compound ("body", tree ("ACTOR-LATEX"));
  doc << compound ("view", tree ("tmfs://view/7/actor-latex-test.ath"));
  ExecutionScope scope (context);
  tree expanded= latex_expand (doc);

  QCOMPARE (extract (expanded, "body"), tree (DOCUMENT, "ACTOR-LATEX"));
  QCOMPARE (extract (expanded, "view"), tree (""));
}

QTEST_MAIN (TestExecutionContext)
#include "execution_context_test.moc"
