/******************************************************************************
* MODULE     : actor_transport_test.cpp
* DESCRIPTION: ID-only actor command transport ownership tests
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "actor_transport.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

class TestActorTransport: public QObject {
  Q_OBJECT

private slots:
  void exposesThePublishedSlotInPlace ();
  void appliesBackpressureUntilCompletion ();
  void transfersBlobOwnershipExactlyOnce ();
  void abandonsUnpublishedBlobs ();
};

void
TestActorTransport::exposesThePublishedSlotInPlace () {
  actor_command_transport transport (3);
  actor_command_transport::writable_command writable;
  QVERIFY (transport.acquire (writable));
  actor_command_record* address= writable.record;
  writable.record->command_id= 17;
  writable.record->view_id= 29;
  writable.record->kind= actor_command_kind::text_input;
  QVERIFY (transport.publish (writable.slot));

  actor_command_transport::readable_command readable;
  QVERIFY (transport.wait_command (readable));
  QCOMPARE (readable.slot, writable.slot);
  QCOMPARE (readable.record, static_cast<const actor_command_record*> (address));
  QCOMPARE (readable.record->command_id, std::uint64_t (17));
  QCOMPARE (readable.record->view_id, athena_view_id (29));
  QCOMPARE (readable.record->kind, actor_command_kind::text_input);
  QVERIFY (transport.complete (readable.slot));
}

void
TestActorTransport::appliesBackpressureUntilCompletion () {
  actor_command_transport transport (2);
  actor_command_transport::writable_command first, second;
  QVERIFY (transport.acquire (first));
  QVERIFY (transport.acquire (second));

  std::atomic<bool> acquired (false);
  std::thread producer ([&] {
    actor_command_transport::writable_command third;
    acquired.store (transport.acquire (third), std::memory_order_release);
  });
  QTest::qWait (20);
  QVERIFY (!acquired.load (std::memory_order_acquire));

  QVERIFY (transport.publish (first.slot));
  actor_command_transport::readable_command command;
  QVERIFY (transport.wait_command (command));
  QVERIFY (transport.complete (command.slot));
  producer.join ();
  QVERIFY (acquired.load (std::memory_order_acquire));
  transport.close ();
}

void
TestActorTransport::transfersBlobOwnershipExactlyOnce () {
  actor_blob_registry& registry= actor_blob_registry::instance ();
  std::size_t before= registry.outstanding ();
  actor_blob_reservation writable= registry.allocate (6);
  QVERIFY (writable);
  const char bytes[]= {'a', 't', 'h', 'e', 'n', 'a'};
  std::memcpy (writable.data (), bytes, sizeof (bytes));
  const std::byte* address= writable.data ();
  athena_blob_id id= writable.publish ();

  owned_actor_blob owned= registry.take (id);
  QVERIFY (owned);
  QCOMPARE (owned.data (), address);
  QCOMPARE (owned.size (), sizeof (bytes));
  QCOMPARE (std::memcmp (owned.data (), bytes, sizeof (bytes)), 0);
  QVERIFY (!registry.take (id));
  QCOMPARE (registry.outstanding (), before);
}

void
TestActorTransport::abandonsUnpublishedBlobs () {
  actor_blob_registry& registry= actor_blob_registry::instance ();
  std::size_t before= registry.outstanding ();
  {
    actor_blob_reservation writable= registry.allocate (128);
    QVERIFY (writable);
    QCOMPARE (registry.outstanding (), before + 1);
  }
  QCOMPARE (registry.outstanding (), before);
}

QTEST_MAIN (TestActorTransport)
#include "actor_transport_test.moc"
