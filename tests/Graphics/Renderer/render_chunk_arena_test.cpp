/******************************************************************************
* MODULE     : render_chunk_arena_test.cpp
* DESCRIPTION: Zero-copy render transport ownership and backpressure tests
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "render_chunk_arena.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

class TestRenderChunkArena: public QObject {
  Q_OBJECT

private slots:
  void transfersWithoutPayloadCopies ();
  void discardsUnpublishedSlots ();
  void appliesBackpressureAndWakesOnCompletion ();
  void closeWakesBlockedConsumer ();
};

void
TestRenderChunkArena::transfersWithoutPayloadCopies () {
  render_chunk_arena arena (3, 64);
  render_chunk_arena::writable_chunk chunk;
  QVERIFY (arena.acquire (chunk));
  QCOMPARE (chunk.capacity, std::size_t (64));
  const char text[]= "render chunk";
  std::memcpy (chunk.data, text, sizeof (text));

  render_chunk_descriptor submitted;
  submitted.slot= chunk.slot;
  submitted.used= sizeof (text);
  submitted.buffer_generation= 7;
  submitted.frame_generation= 11;
  submitted.damage= {1, 2, 30, 40};
  submitted.final_chunk= true;
  QVERIFY (arena.publish (submitted));

  render_chunk_descriptor received;
  QVERIFY (arena.wait_submission (received));
  QCOMPARE (received.slot, submitted.slot);
  QCOMPARE (received.used, submitted.used);
  QCOMPARE (received.buffer_generation, std::uint64_t (7));
  QCOMPARE (received.frame_generation, std::uint64_t (11));
  QCOMPARE (received.damage.x2, std::int64_t (30));
  QCOMPARE (arena.payload (received.slot),
            static_cast<const std::byte*> (chunk.data));
  QCOMPARE (std::memcmp (arena.payload (received.slot), text, sizeof (text)), 0);
  QVERIFY (arena.complete (received.slot));
}

void
TestRenderChunkArena::discardsUnpublishedSlots () {
  render_chunk_arena arena (2, 16);
  render_chunk_arena::writable_chunk first, second, reused;
  QVERIFY (arena.acquire (first));
  QVERIFY (arena.acquire (second));
  QVERIFY (arena.discard (first.slot));
  QVERIFY (arena.acquire (reused));
  QCOMPARE (reused.slot, first.slot);
  arena.close ();
}

void
TestRenderChunkArena::appliesBackpressureAndWakesOnCompletion () {
  render_chunk_arena arena (2, 16);
  render_chunk_arena::writable_chunk first, second;
  QVERIFY (arena.acquire (first));
  QVERIFY (arena.acquire (second));

  std::atomic<bool> acquired (false);
  std::thread producer ([&] {
    render_chunk_arena::writable_chunk third;
    acquired.store (arena.acquire (third), std::memory_order_release);
  });
  QTest::qWait (20);
  QVERIFY (!acquired.load (std::memory_order_acquire));

  render_chunk_descriptor descriptor;
  descriptor.slot= first.slot;
  descriptor.used= 1;
  QVERIFY (arena.publish (descriptor));
  render_chunk_descriptor received;
  QVERIFY (arena.wait_submission (received));
  QVERIFY (arena.complete (received.slot));
  producer.join ();
  QVERIFY (acquired.load (std::memory_order_acquire));
  arena.close ();
}

void
TestRenderChunkArena::closeWakesBlockedConsumer () {
  render_chunk_arena arena (2, 16);
  std::atomic<bool> result (true);
  std::thread consumer ([&] {
    render_chunk_descriptor descriptor;
    result.store (arena.wait_submission (descriptor),
                  std::memory_order_release);
  });
  QTest::qWait (20);
  arena.close ();
  consumer.join ();
  QVERIFY (!result.load (std::memory_order_acquire));
}

QTEST_MAIN (TestRenderChunkArena)
#include "render_chunk_arena_test.moc"
