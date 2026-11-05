/******************************************************************************
* MODULE     : link_registry_test.cpp
* DESCRIPTION: Concurrent link index lifetime and live locus ownership
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************/

#include "link.hpp"
#include <QtTest/QtTest>
#include <atomic>
#include <thread>
#include <vector>

class LinkRegistryTest: public QObject {
  Q_OBJECT
private slots:
  void concurrentRepositories ();
  void liveLociStayWithOwner ();
};

void
LinkRegistryTest::concurrentRepositories () {
  std::atomic<int> ready {0}, published {0}, queried {0};
  std::atomic<bool> go {false}, okay {true};
  std::vector<std::thread> workers;
  for (int w= 0; w < 4; ++w) {
    workers.emplace_back ([&] {
      tree vertex (ID, "shared-link-registry-test");
      ++ready;
      while (!go.load ()) std::this_thread::yield ();
      {
        link_repository repo (true);
        repo->insert_link (tree (LINK, "registry-test", tree (ATTR), vertex,
                                tree (URL, "test-target")));
        ++published;
        while (published.load () != 4) std::this_thread::yield ();
        if (N (get_links (vertex)) != 4) okay= false;
        ++queried;
        while (queried.load () != 4) std::this_thread::yield ();
      }
      for (int i= 0; i < 2000; ++i) {
        list<tree> result;
        {
          link_repository repo (true);
          repo->insert_link (tree (LINK, "registry-test", tree (ATTR),
                                  vertex, tree (URL, "test-target")));
          result= get_links (vertex);
          if (is_nil (result)) okay= false;
          declare_visited ("registry-test-visited");
          if (!has_been_visited ("registry-test-visited")) okay= false;
          bool found= false;
          for (list<string> ts= all_link_types (); !is_nil (ts); ts= ts->next)
            found= found || ts->item == "registry-test";
          if (!found) okay= false;
        }
        // Results must remain independent while other owners unregister/retype.
        for (list<tree> p= result; !is_nil (p); p= p->next) {
          if (p->item[3][0] != "test-target") okay= false;
          p->item[3][0]= "caller-owned-result";
        }
      }
    });
  }
  while (ready.load () != 4) std::this_thread::yield ();
  go= true;
  for (auto& worker: workers) worker.join ();
  QVERIFY (okay.load ());
  QVERIFY (is_nil (get_links (tree (ID, "shared-link-registry-test"))));
  QVERIFY (is_nil (all_link_types ()));
}

void
LinkRegistryTest::liveLociStayWithOwner () {
  std::atomic<int> ready {0}, checked {0};
  std::atomic<bool> okay {true};
  std::vector<std::thread> workers;
  for (int w= 0; w < 2; ++w) {
    workers.emplace_back ([&, w] {
      tree body (w == 0? "first owner": "second owner");
      {
        link_repository repo (true);
        repo->insert_locus ("shared-locus-registry-test", body);
        ++ready;
        while (ready.load () != 2) std::this_thread::yield ();
        list<tree> loci= get_trees ("shared-locus-registry-test");
        if (is_nil (loci) || !is_nil (loci->next) ||
            loci->item != body || is_nil (get_ids (body))) okay= false;
        ++checked;
        while (checked.load () != 2) std::this_thread::yield ();
      }
      if (!is_nil (get_trees ("shared-locus-registry-test")) ||
          !is_nil (get_ids (body))) okay= false;
    });
  }
  for (auto& worker: workers) worker.join ();
  QVERIFY (okay.load ());
}

QTEST_GUILESS_MAIN (LinkRegistryTest)
#include "link_registry_test.moc"
