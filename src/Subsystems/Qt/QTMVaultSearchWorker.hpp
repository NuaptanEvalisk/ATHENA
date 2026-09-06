#ifndef QTMVAULTSEARCHWORKER_HPP
#define QTMVAULTSEARCHWORKER_HPP

#include "QTMVaultSearch.hpp"
#include "qt_utilities.hpp"
#include <QThreadPool>
#include <QRunnable>
#include <QTimer>
#include <atomic>
#include <memory>
#include <numeric>
#include <string>

struct VaultSearchOptions {
  QString query, enunciation, person, root;
  bool caseInsensitive= true;
  bool fuzzy= false;
};

struct VaultSearchControl {
  std::atomic<bool> cancelled {false};
};

struct VaultSearchProgress {
  bool inspecting= false;
  int completed= 0, total= 0, candidates= 0, matchedFiles= 0, hits= 0;
};

QThreadPool& vault_search_workers ();
int vault_search_worker_limit ();
int vault_search_worker_count ();
tree vault_search_read_body (const QString& file);
bool vault_search_cancelled ();

class VaultSearchCancellationScope {
  const std::atomic<bool>* previous;
public:
  explicit VaultSearchCancellationScope (const std::atomic<bool>* flag);
  ~VaultSearchCancellationScope ();
};

// Each file slot has one writer. Only the GUI reads results, after every
// worker has released its slot; document trees never cross this boundary.
template<typename Result> struct VaultSearchState : VaultSearchControl {
  VaultSearchOptions options;
  std::vector<std::string> files;
  std::vector<unsigned char> possible;
  std::vector<size_t> candidates;
  std::vector<std::vector<Result>> results;
  std::atomic<size_t> next {0};
  std::atomic<int> workers {0}, completed {0}, matchedFiles {0}, hits {0}, candidateCount {0};
  bool inspecting= false;
};

template<typename Result, typename Scan, typename Report, typename Complete>
std::shared_ptr<VaultSearchControl>
start_vault_search (QObject* owner, std::vector<std::string> files,
                    VaultSearchOptions options, Scan scan,
                    Report report, Complete complete) {
  auto state= std::make_shared<VaultSearchState<Result>> ();
  vault_search_workers ().setMaxThreadCount (vault_search_worker_count ());
  state->files= std::move (files);
  state->options= std::move (options);
  state->possible.resize (state->files.size ());
  state->results.resize (state->files.size ());
  VaultRawSearchPrefilter filter (state->options.query,
    state->options.caseInsensitive, state->options.fuzzy);
  if (!filter.isEffective ()) {
    state->inspecting= true;
    state->candidates.resize (state->files.size ());
    std::iota (state->candidates.begin (), state->candidates.end (), 0);
    state->candidateCount= (int) state->files.size ();
  }
  auto launch= [state, scan] () {
    state->next= 0;
    state->completed= 0;
    size_t total= state->inspecting ? state->candidates.size () :
                                    state->files.size ();
    int count= std::min (total, (size_t) vault_search_workers ().maxThreadCount ());
    state->workers= count;
    for (int worker= 0; worker<count; ++worker)
      vault_search_workers ().start (QRunnable::create ([state, scan] () {
        VaultSearchCancellationScope cancellation (&state->cancelled);
        try {
          const auto options= state->options;
          const tree query (from_qstring (options.query));
          VaultRawSearchPrefilter filter (options.query,
            options.caseInsensitive, options.fuzzy);
          const size_t total= state->inspecting ? state->candidates.size () :
                                                 state->files.size ();
          while (!state->cancelled.load (std::memory_order_relaxed)) {
            size_t slot= state->next.fetch_add (1, std::memory_order_relaxed);
            if (slot >= total) break;
            size_t index= state->inspecting ? state->candidates[slot] : slot;
            QString file= QString::fromStdString (state->files[index]);
            if (!state->inspecting) {
              state->possible[index]= filter.fileMayMatch (file);
              if (state->possible[index]) state->candidateCount.fetch_add (1);
            }
            else {
              auto& hits= state->results[index];
              try {
                tree body= vault_search_read_body (file);
                if (!vault_search_cancelled ())
                  scan (body, url_system (from_qstring (file)), query,
                        options, hits);
              }
              catch (...) { hits.clear (); }
              if (vault_search_cancelled ()) { hits.clear (); break; }
              if (!hits.empty ()) state->matchedFiles.fetch_add (1);
              state->hits.fetch_add ((int) hits.size ());
            }
            if (vault_search_cancelled ()) break;
            state->completed.fetch_add (1, std::memory_order_relaxed);
          }
        }
        catch (...) { state->cancelled= true; }
        state->workers.fetch_sub (1, std::memory_order_acq_rel);
      }));
  };
  auto* timer= new QTimer (owner);
  timer->setInterval (75);
  QObject::connect (owner, &QObject::destroyed, timer,
                    [state] { state->cancelled= true; });
  QObject::connect (timer, &QTimer::timeout, timer,
    [state, timer, launch, report, complete] () {
      bool idle= state->workers.load (std::memory_order_acquire) == 0;
      if (idle && !state->inspecting && !state->cancelled) {
        for (size_t i= 0; i<state->files.size (); ++i)
          if (state->possible[i]) state->candidates.push_back (i);
        state->inspecting= true;
        launch ();
        idle= state->workers.load (std::memory_order_acquire) == 0;
      }
      VaultSearchProgress progress;
      progress.inspecting= state->inspecting;
      progress.completed= state->completed.load ();
      progress.total= state->inspecting ? (int) state->candidates.size () :
                                          (int) state->files.size ();
      progress.candidates= state->candidateCount.load ();
      progress.matchedFiles= state->matchedFiles.load ();
      progress.hits= state->hits.load ();
      report (progress);
      if (!idle) return;
      timer->stop ();
      timer->deleteLater ();
      std::vector<Result> collected;
      collected.reserve (progress.hits);
      for (auto& hits: state->results)
        std::move (hits.begin (), hits.end (), std::back_inserter (collected));
      complete (std::move (collected), progress, state->cancelled.load ());
    });
  launch ();
  timer->start ();
  return state;
}

#endif
