/******************************************************************************
* MODULE     : render_service_test.cpp
* DESCRIPTION: Shared RenderService scheduling and isolation tests
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "render_service.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

class TestRenderService: public QObject {
  Q_OBJECT

private slots:
  void usesOneWorkerForMultipleActors ();
};

void
TestRenderService::usesOneWorkerForMultipleActors () {
  render_service& service= render_service::instance ();
  std::mutex lock;
  std::condition_variable changed;
  std::vector<std::uint64_t> buffers;
  std::set<std::thread::id> consumers;

  auto processor= [&] (const render_chunk_descriptor& descriptor,
                       const std::byte* payload) {
    QCOMPARE (static_cast<unsigned char> (payload[0]),
              static_cast<unsigned char> (descriptor.buffer_generation));
    {
      std::lock_guard<std::mutex> guard (lock);
      buffers.push_back (descriptor.buffer_generation);
      consumers.insert (std::this_thread::get_id ());
    }
    changed.notify_all ();
  };

  auto first= service.connect (processor, 2, 32);
  auto second= service.connect (processor, 2, 32);
  QVERIFY (first != nullptr);
  QVERIFY (second != nullptr);

  auto submit= [] (const std::shared_ptr<render_connection>& connection,
                   std::uint64_t generation) {
    render_chunk_arena::writable_chunk chunk;
    QVERIFY (connection->acquire (chunk));
    chunk.data[0]= static_cast<std::byte> (generation);
    render_chunk_descriptor descriptor;
    descriptor.slot= chunk.slot;
    descriptor.used= 1;
    descriptor.buffer_generation= generation;
    descriptor.frame_generation= 1;
    descriptor.final_chunk= true;
    QVERIFY (connection->publish (descriptor));
  };

  std::thread producer_one ([&] { submit (first, 1); });
  std::thread producer_two ([&] { submit (second, 2); });
  producer_one.join ();
  producer_two.join ();

  {
    std::unique_lock<std::mutex> guard (lock);
    QVERIFY (changed.wait_for (guard, std::chrono::seconds (1),
                               [&] { return buffers.size () == 2; }));
  }
  QCOMPARE (consumers.size (), std::size_t (1));
  QCOMPARE (*consumers.begin (), service.worker_thread ());
  std::sort (buffers.begin (), buffers.end ());
  QCOMPARE (buffers, std::vector<std::uint64_t> ({1, 2}));

  first->retire ();
  second->retire ();
  first.reset ();
  second.reset ();
  service.shutdown ();
}

QTEST_MAIN (TestRenderService)
#include "render_service_test.moc"
