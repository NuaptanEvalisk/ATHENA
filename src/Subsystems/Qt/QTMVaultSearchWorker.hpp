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
#include <map>
#include <set>
#include <algorithm>
#include <QPointer>
#include "Data/Convert/Xml/athena_document_xml.hpp"

enum class VaultSearchMode { Content, Semantic, Hybrid };
struct VaultSemanticHit {
  std::string treePath, semanticRevision;
  double score= 0;
};
using VaultSemanticCandidates= std::map<QString, std::vector<VaultSemanticHit>>;

struct VaultSearchOptions {
  QString query, enunciation, person, root;
  std::string serializedQuery;
  bool caseInsensitive= true;
  bool fuzzy= false;
  bool nodeModel= false;
  VaultSearchMode mode= VaultSearchMode::Content;
  std::shared_ptr<const VaultSemanticCandidates> semanticCandidates;
  std::shared_ptr<std::atomic<int>> staleCandidates= std::make_shared<std::atomic<int>> (0);
};

struct VaultSearchControl {
  std::atomic<bool> cancelled {false};
};

struct VaultSearchProgress {
  bool inspecting= false;
  int completed= 0, total= 0, candidates= 0, matchedFiles= 0, hits= 0;
  bool semanticPending= false;
  QString diagnostic;
};

void vault_search_semantic_candidates (QObject* owner,
  const std::vector<std::string>& files, const VaultSearchOptions& options,
  std::shared_ptr<VaultSearchControl> control,
  std::function<void(std::shared_ptr<const VaultSemanticCandidates>, QString)> complete);
void vault_search_semantic_matches (tree body, url file,
  const VaultSearchOptions& options, std::vector<VaultContentMatch>& matches);
QString vault_search_node_uuid (tree body, path position);
QString vault_search_identity (tree body, url file, path position);

QThreadPool& vault_search_workers ();
int vault_search_worker_limit ();
int vault_search_worker_count ();
tree vault_search_read_body (const QString& file, bool semantic= false);
bool vault_search_cancelled ();
bool vault_search_has_live_source (const QString& file);

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
  std::atomic<int> workers {0}, completed {0}, matchedFiles {0}, hits {0}, candidateCount {0}, failedFiles {0};
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
  if (state->options.mode == VaultSearchMode::Semantic || !state->options.serializedQuery.empty () || !filter.isEffective ()) {
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
          const tree query= options.serializedQuery.empty () ? tree (from_qstring (options.query)) :
            athena::document::read_xml_v2 (options.serializedQuery, athena::document::xml_kind::fragment);
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
              state->possible[index]= vault_search_has_live_source (file) || filter.fileMayMatch (file);
              if (state->possible[index]) state->candidateCount.fetch_add (1);
            }
            else {
              auto& hits= state->results[index];
              try {
                tree body= vault_search_read_body (file, options.mode == VaultSearchMode::Semantic);
                if (!vault_search_cancelled ())
                  scan (body, url_system (from_qstring (file)), query,
                        options, hits);
              }
              catch (...) { hits.clear (); state->failedFiles.fetch_add (1); }
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
      if (state->failedFiles.load () > 0)
        progress.diagnostic= QString ("Could not inspect %1 source file(s).").arg (state->failedFiles.load ());
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

template<typename Result> struct VaultCombinedSearch {
  std::shared_ptr<VaultSearchControl> control= std::make_shared<VaultSearchControl> ();
  std::vector<std::shared_ptr<VaultSearchControl>> children;
  std::vector<Result> content, semantic;
  VaultSearchProgress progress;
  bool contentDone= false, semanticDone= false, finished= false;
};

template<typename Result, typename Scan, typename Report, typename Complete>
std::shared_ptr<VaultSearchControl>
start_vault_ranked_search (QObject* owner, std::vector<std::string> files,
                   VaultSearchOptions options, Scan scan,
                   Report report, Complete complete) {
  if (options.mode == VaultSearchMode::Content)
    return start_vault_search<Result> (owner, std::move (files),
      std::move (options), scan, report, complete);

  auto state= std::make_shared<VaultCombinedSearch<Result>> ();
  auto* lifetime= new QObject (owner);
  QPointer<QObject> alive= lifetime;
  state->contentDone= options.mode == VaultSearchMode::Semantic;
  state->progress.semanticPending= true;
  QObject::connect (lifetime, &QObject::destroyed, [state] {
    state->control->cancelled= true;
    for (const auto& child: state->children) child->cancelled= true;
  });
  auto finish= [state, alive, complete, mode= options.mode, stale= options.staleCandidates] {
    if (!alive || state->finished) return;
    const bool cancelled= state->control->cancelled.load ();
    if (!cancelled && (!state->contentDone || !state->semanticDone)) return;
    state->finished= true;
    std::vector<Result> results;
    if (!cancelled) {
      std::stable_sort (state->content.begin (), state->content.end (), [] (const Result& a, const Result& b) {
        if (a.exact != b.exact) return a.exact;
        return a.score > b.score;
      });
      std::stable_sort (state->semantic.begin (), state->semantic.end (), [] (const Result& a, const Result& b) {
        return a.semanticScore > b.semanticScore;
      });
      std::map<QString, std::size_t> positions;
      auto merge= [&] (const std::vector<Result>& branch, bool semantic) {
        std::set<QString> seen;
        int rank= 0;
        for (const auto& hit: branch) {
          const QString key= hit.sourceUuid.isEmpty () ? hit.identityKey : hit.sourceUuid;
          if (!seen.insert (key).second) continue;
          const double contribution= 1.0 / (60.0 + ++rank);
          auto inserted= positions.emplace (key, results.size ());
          if (inserted.second) { results.push_back (hit); results.back ().relevance= contribution; }
          else {
            auto& prior= results[inserted.first->second];
            prior.relevance+= contribution;
            if (semantic) { prior.semantic= true; prior.semanticScore= hit.semanticScore; }
          }
        }
      };
      merge (state->content, false); merge (state->semantic, true);
      std::stable_sort (results.begin (), results.end (), [mode] (const Result& a, const Result& b) {
        return mode == VaultSearchMode::Semantic ? a.semanticScore > b.semanticScore : a.relevance > b.relevance;
      });
      if (results.size () > 200) results.resize (200);
    }
    state->progress.semanticPending= false;
    if (stale->load () > 0) {
      if (!state->progress.diagnostic.isEmpty ()) state->progress.diagnostic+= "\n";
      state->progress.diagnostic+=
        QString ("%1 outdated semantic candidate(s) omitted; the index is catching up.").arg (stale->load ());
    }
    state->progress.hits= int (results.size ());
    std::set<QString> matchedFiles;
    for (const auto& hit: results) matchedFiles.insert (hit.relPath);
    state->progress.matchedFiles= int (matchedFiles.size ());
    complete (std::move (results), state->progress, cancelled);
    if (alive) alive->deleteLater ();
  };
  if (!state->contentDone) {
    auto contentOptions= options; contentOptions.mode= VaultSearchMode::Content;
    state->children.push_back (start_vault_search<Result> (lifetime, files,
      std::move (contentOptions), scan,
      [state, report] (const VaultSearchProgress& value) {
        const auto diagnostic= state->progress.diagnostic;
        state->progress= value; state->progress.diagnostic= diagnostic;
        state->progress.semanticPending= !state->semanticDone;
        report (state->progress);
      }, [state, finish] (std::vector<Result> hits, const VaultSearchProgress& value, bool cancelled) {
        state->content= std::move (hits); state->contentDone= true;
        state->progress.completed= value.completed; state->progress.total= value.total;
        if (!value.diagnostic.isEmpty ()) state->progress.diagnostic+= "\n" + value.diagnostic;
        if (cancelled) state->control->cancelled= true;
        finish ();
      }));
  }
  report (state->progress);
  vault_search_semantic_candidates (lifetime, files, options, state->control,
    [state, alive, options, scan, report, finish] (std::shared_ptr<const VaultSemanticCandidates> candidates, QString diagnostic) mutable {
      if (!alive || state->finished) return;
      if (!diagnostic.isEmpty ()) {
        if (!state->progress.diagnostic.isEmpty ()) state->progress.diagnostic+= "\n";
        state->progress.diagnostic+= diagnostic;
      }
      if (!candidates || candidates->empty ()) { state->semanticDone= true; finish (); return; }
      std::vector<std::string> selected;
      for (const auto& entry: *candidates) selected.push_back (entry.first.toStdString ());
      options.mode= VaultSearchMode::Semantic; options.semanticCandidates= std::move (candidates);
      state->children.push_back (start_vault_search<Result> (alive, std::move (selected),
        std::move (options), scan,
        [state, report] (const VaultSearchProgress& value) {
          auto progress= value; progress.semanticPending= true; progress.diagnostic= state->progress.diagnostic;
          report (progress);
        }, [state, finish] (std::vector<Result> hits, const VaultSearchProgress& value, bool cancelled) {
          state->semantic= std::move (hits); state->semanticDone= true;
          state->progress.completed= std::max (state->progress.completed, value.completed);
          state->progress.total= std::max (state->progress.total, value.total);
          if (!value.diagnostic.isEmpty ()) state->progress.diagnostic+= "\n" + value.diagnostic;
          if (cancelled) state->control->cancelled= true;
          finish ();
        }));
    });
  auto* timer= new QTimer (lifetime);
  QObject::connect (timer, &QTimer::timeout, lifetime, [state, finish] {
    if (state->control->cancelled) finish ();
  });
  timer->start (75);
  return state->control;
}

#endif
