/******************************************************************************
* MODULE     : buffer_actor_test.cpp
* DESCRIPTION: BufferActor ownership, ordering, and shutdown behavior
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "buffer_actor.hpp"
#include "tm_buffer.hpp"

#include <atomic>
#include <thread>
#include <vector>

class TestBufferActor: public QObject {
  Q_OBJECT

private slots:
  void startsLazily ();
  void ownsCommandsAndDocumentContext ();
  void preservesSynchronousInvocationContext ();
  void drainsInFifoOrderAndRejectsAfterShutdown ();
};

void
TestBufferActor::startsLazily () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-lazy-test.ath"));
  QCOMPARE (buffer->actor->owner_thread (), std::thread::id ());
  QCOMPARE (buffer->actor->completed_commands (), std::uint64_t (0));
  tm_delete (buffer);
}

void
TestBufferActor::preservesSynchronousInvocationContext () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-context-test.ath"));
  string expected_view= "tmfs://view/7/actor-context-test.ath";
  bool valid_context= buffer->actor->invoke_native ([&] {
    const SchemeExecutionContext* context=
      current_scheme_execution_context ();
    return context != nullptr && context->actor == buffer->actor &&
           as_string (context->view_id) == expected_view &&
           context->has (SCHEME_CAPABILITY_BUFFER);
  }, nullptr, expected_view);

  QVERIFY (valid_context);
  tm_delete (buffer);
}

void
TestBufferActor::ownsCommandsAndDocumentContext () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-test.ath"));
  std::thread::id caller= std::this_thread::get_id ();
  std::thread::id executor;
  bool valid_context= false;

  buffer->actor->invoke_native ([&] {
    executor= std::this_thread::get_id ();
    const SchemeExecutionContext* context=
      current_scheme_execution_context ();
    valid_context= context != nullptr && context->actor == buffer->actor &&
                   context->command_id != 0 &&
                   &current_document_tree () == &buffer->document;
  });

  QVERIFY (executor != caller);
  QCOMPARE (executor, buffer->actor->owner_thread ());
  QVERIFY (valid_context);
  tm_delete (buffer);
}

void
TestBufferActor::drainsInFifoOrderAndRejectsAfterShutdown () {
  tm_buffer buffer= tm_new<tm_buffer_rep> (url ("actor-order-test.ath"));
  std::vector<int> observed;
  for (int value= 1; value <= 4; ++value)
    QVERIFY (buffer->actor->post_native (
      [&observed, value] { observed.push_back (value); }));

  buffer->actor->wait_until_idle ();
  QCOMPARE (observed, std::vector<int> ({ 1, 2, 3, 4 }));
  QCOMPARE (buffer->actor->completed_commands (), std::uint64_t (4));

  buffer->actor->shutdown ();
  QVERIFY (!buffer->actor->post_native ([] {}));
  tm_delete (buffer);
}

QTEST_MAIN (TestBufferActor)
#include "buffer_actor_test.moc"
