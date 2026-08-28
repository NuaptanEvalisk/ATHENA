/******************************************************************************
* MODULE     : artifact_delegation_queue.cpp
* DESCRIPTION: In-memory FIFO for delegated artifact definition spans
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
*/

#include "artifact_delegation_queue.hpp"

#include "ATHENA/Data/artifact_range_llm.hpp"
#include "rag_delegation_crypto.hpp"
#include "tm_ostream.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace athena::mcp {
namespace {

using Clock= std::chrono::steady_clock;

QJsonObject error_result (const std::string& message) {
  QJsonObject result;
  result["ok"]= false;
  result["error"]= QString::fromStdString (message);
  return result;
}

enum class TaskState { Queued, Running, Completed, Failed, Cancelled };

struct ArtifactTask {
  std::string id;
  AthenaArtifactRangeRequest request;
  TaskState state= TaskState::Queued;
  std::vector<int> offsets;
  std::string error;
};

struct ArtifactJob {
  std::string id;
  std::string principal;
  std::string submission_id;
  std::vector<ArtifactTask> tasks;
  std::vector<size_t> events;
  std::string error;
  bool cancel_requested= false;
  size_t stored_bytes= 0;
  Clock::time_point last_access= Clock::now ();
};

bool terminal (const ArtifactJob& job) {
  for (const ArtifactTask& task: job.tasks)
    if (task.state == TaskState::Queued || task.state == TaskState::Running)
      return false;
  return true;
}

bool valid_offsets (const AthenaArtifactRangeRequest& request,
                    const std::vector<int>& offsets) {
  if (offsets.empty ()) return true;
  if (std::find (offsets.begin (), offsets.end (), 0) == offsets.end ())
    return false;
  std::set<int> allowed;
  for (const auto& candidate: request.paragraphs)
    allowed.insert (candidate.first);
  for (size_t i=0; i<offsets.size (); i++) {
    if (!allowed.count (offsets[i])) return false;
    if (i && offsets[i] != offsets[i - 1] + 1) return false;
  }
  return std::is_sorted (offsets.begin (), offsets.end ()) &&
         std::adjacent_find (offsets.begin (), offsets.end ()) == offsets.end ();
}

std::string submission_key (const std::string& principal,
                            const std::string& submission) {
  return principal + "\n" + submission;
}

} // namespace

class ArtifactDelegationQueue::Impl {
public:
  explicit Impl (ArtifactDelegationQueueOptions value)
    : options (std::move (value)), worker ([this] { run (); }) {}

  ~Impl () {
    {
      std::lock_guard<std::mutex> guard (mutex);
      stopping= true;
    }
    changed.notify_all ();
    if (worker.joinable ()) worker.join ();
  }

  QJsonObject submit (const std::string& principal,
                      const QJsonObject& params) {
    QByteArray plain= QJsonDocument (params).toJson (QJsonDocument::Compact);
    if (plain.size () > options.max_plaintext_bytes)
      return error_result ("artifact definition-span job exceeds plaintext limit");
    std::string submission=
      params.value ("submission_id").toString ().toStdString ();
    if (submission.empty ()) return error_result ("submission_id is required");

    QJsonArray catalog_json= params.value ("catalog").toArray ();
    std::vector<std::string> catalog;
    catalog.reserve ((size_t) catalog_json.size ());
    for (const QJsonValue& value: catalog_json) {
      if (!value.isString ()) return error_result ("catalog entry is not a string");
      catalog.push_back (value.toString ().toStdString ());
    }
    QJsonArray requests= params.value ("requests").toArray ();
    if (requests.isEmpty () ||
        requests.size () > options.max_requests_per_job)
      return error_result ("invalid artifact definition-span request count");

    std::vector<ArtifactTask> tasks;
    std::set<std::string> ids;
    tasks.reserve ((size_t) requests.size ());
    for (const QJsonValue& value: requests) {
      QJsonObject object= value.toObject ();
      ArtifactTask task;
      task.id= object.value ("id").toString ().toStdString ();
      task.request.keyword_latex=
        object.value ("keyword_latex").toString ().toStdString ();
      if (task.id.empty () || !ids.insert (task.id).second)
        return error_result ("artifact request ids must be nonempty and unique");
      bool includes_zero= false;
      for (const QJsonValue& candidate_value:
           object.value ("candidates").toArray ()) {
        QJsonObject candidate= candidate_value.toObject ();
        int offset= candidate.value ("offset").toInt ();
        int catalog_index= candidate.value ("catalog").toInt (-1);
        if (catalog_index < 0 || catalog_index >= (int) catalog.size ())
          return error_result ("artifact request references an invalid catalog entry");
        task.request.paragraphs.push_back ({offset, catalog[catalog_index]});
        includes_zero= includes_zero || offset == 0;
      }
      if (!includes_zero || task.request.paragraphs.empty ())
        return error_result ("artifact request must include paragraph 0");
      tasks.push_back (std::move (task));
    }

    std::unique_lock<std::mutex> lock (mutex);
    cleanup_locked ();
    std::string key= submission_key (principal, submission);
    auto existing= submissions.find (key);
    if (existing != submissions.end ()) {
      auto found= jobs.find (existing->second);
      if (found != jobs.end ()) return submit_result_locked (*found->second);
      submissions.erase (existing);
    }
    if (stored_items + tasks.size () > (size_t) options.max_queued_items ||
        stored_bytes + (size_t) plain.size () >
          (size_t) options.max_stored_bytes)
      return error_result ("artifact definition-span queue is full");

    auto job= std::make_shared<ArtifactJob> ();
    job->id= athena::rag::delegation::random_hex_id (16);
    job->principal= principal;
    job->submission_id= submission;
    job->tasks= std::move (tasks);
    job->stored_bytes= (size_t) plain.size ();
    jobs[job->id]= job;
    submissions[key]= job->id;
    stored_items += job->tasks.size ();
    stored_bytes += job->stored_bytes;
    for (size_t index=0; index<job->tasks.size (); index++)
      queue.push_back ({job, index});
    athena_spdlog_info (
      "artifact delegation: queued job=" + job->id +
      " requests=" + std::to_string (job->tasks.size ()) +
      " queue=" + std::to_string (queue.size ()));
    QJsonObject result= submit_result_locked (*job);
    lock.unlock ();
    changed.notify_all ();
    return result;
  }

  QJsonObject wait (const std::string& principal, const QJsonObject& params) {
    std::unique_lock<std::mutex> lock (mutex);
    cleanup_locked ();
    auto job= find_job_locked (principal, params);
    if (!job) return error_result ("artifact definition-span job not found");
    size_t cursor= (size_t) std::max (0, params.value ("cursor").toInt ());
    if (cursor > job->events.size ()) return error_result ("invalid result cursor");
    int wait_ms= std::clamp (params.value ("wait_ms").toInt (0), 0, 20000);
    if (cursor == job->events.size () && !terminal (*job) && wait_ms > 0)
      changed.wait_for (lock, std::chrono::milliseconds (wait_ms), [&] {
        return stopping || cursor < job->events.size () || terminal (*job);
      });
    job->last_access= Clock::now ();
    return status_result_locked (*job, cursor);
  }

  QJsonObject cancel (const std::string& principal,
                      const QJsonObject& params) {
    std::lock_guard<std::mutex> guard (mutex);
    auto job= find_job_locked (principal, params);
    if (!job) return error_result ("artifact definition-span job not found");
    job->cancel_requested= true;
    for (size_t i=0; i<job->tasks.size (); i++) {
      ArtifactTask& task= job->tasks[i];
      if (task.state == TaskState::Queued) {
        task.state= TaskState::Cancelled;
        job->events.push_back (i);
      }
    }
    job->last_access= Clock::now ();
    changed.notify_all ();
    return status_result_locked (*job, 0);
  }

  QJsonObject acknowledge (const std::string& principal,
                           const QJsonObject& params) {
    std::lock_guard<std::mutex> guard (mutex);
    auto job= find_job_locked (principal, params);
    if (!job) return error_result ("artifact definition-span job not found");
    if (!terminal (*job)) return error_result ("cannot acknowledge an active job");
    erase_job_locked (job);
    QJsonObject result;
    result["ok"]= true;
    return result;
  }

  QJsonObject counts () const {
    std::lock_guard<std::mutex> guard (mutex);
    size_t queued= 0, running= 0;
    for (const auto& item: jobs)
      for (const ArtifactTask& task: item.second->tasks) {
        queued += task.state == TaskState::Queued;
        running += task.state == TaskState::Running;
      }
    QJsonObject result;
    result["queued"]= QString::number (queued);
    result["running"]= QString::number (running);
    return result;
  }

  QJsonObject limits () const {
    QJsonObject out;
    out["max_requests_per_job"]= options.max_requests_per_job;
    out["max_plaintext_bytes"]= options.max_plaintext_bytes;
    out["max_queued_items"]= options.max_queued_items;
    out["max_stored_bytes"]= options.max_stored_bytes;
    out["long_poll_ms"]= 20000;
    out["max_in_flight_jobs"]= 4;
    return out;
  }

  bool available () const {
    return !options.model_path.empty () &&
           athena_artifact_range_model_available (options.model_path.string ());
  }

private:
  std::shared_ptr<ArtifactJob> find_job_locked (
    const std::string& principal, const QJsonObject& params) const {
    std::string id= params.value ("job_id").toString ().toStdString ();
    auto found= jobs.find (id);
    if (found == jobs.end () || found->second->principal != principal) return {};
    return found->second;
  }

  QJsonObject submit_result_locked (const ArtifactJob& job) const {
    QJsonObject result;
    result["ok"]= true;
    result["job_id"]= QString::fromStdString (job.id);
    result["accepted"]= (int) job.tasks.size ();
    return result;
  }

  QJsonObject status_result_locked (const ArtifactJob& job,
                                    size_t cursor) const {
    size_t queued= 0, running= 0, completed= 0, failed= 0, cancelled= 0;
    for (const ArtifactTask& task: job.tasks) {
      queued += task.state == TaskState::Queued;
      running += task.state == TaskState::Running;
      completed += task.state == TaskState::Completed;
      failed += task.state == TaskState::Failed;
      cancelled += task.state == TaskState::Cancelled;
    }
    QJsonArray results;
    for (size_t event=cursor; event<job.events.size (); event++) {
      const ArtifactTask& task= job.tasks[job.events[event]];
      if (task.state != TaskState::Completed) continue;
      QJsonObject item;
      item["id"]= QString::fromStdString (task.id);
      QJsonArray offsets;
      for (int offset: task.offsets) offsets.append (offset);
      item["offsets"]= offsets;
      results.append (item);
    }
    QJsonObject counts;
    counts["queued"]= (int) queued;
    counts["running"]= (int) running;
    counts["completed"]= (int) completed;
    counts["failed"]= (int) failed;
    counts["cancelled"]= (int) cancelled;
    QString state= terminal (job) ?
      (job.cancel_requested ? "cancelled":
       (!job.error.empty () || failed ? "failed": "complete")):
      (running ? "running": "queued");
    QJsonObject result;
    result["ok"]= true;
    result["state"]= state;
    result["counts"]= counts;
    result["cursor"]= (int) job.events.size ();
    result["results"]= results;
    if (!job.error.empty ()) result["error"]= QString::fromStdString (job.error);
    return result;
  }

  void erase_job_locked (const std::shared_ptr<ArtifactJob>& job) {
    submissions.erase (submission_key (job->principal, job->submission_id));
    stored_items -= job->tasks.size ();
    stored_bytes -= job->stored_bytes;
    jobs.erase (job->id);
  }

  void cleanup_locked () {
    auto cutoff= Clock::now () -
      std::chrono::seconds (options.result_retention_seconds);
    std::vector<std::shared_ptr<ArtifactJob>> expired;
    for (const auto& entry: jobs)
      if (terminal (*entry.second) && entry.second->last_access < cutoff)
        expired.push_back (entry.second);
    for (const auto& job: expired) erase_job_locked (job);
  }

  void run () {
    while (true) {
      std::vector<std::pair<std::shared_ptr<ArtifactJob>,size_t>> batch;
      {
        std::unique_lock<std::mutex> lock (mutex);
        changed.wait (lock, [&] { return stopping || !queue.empty (); });
        if (stopping) return;
        changed.wait_for (lock, std::chrono::milliseconds (10), [&] {
          return stopping || queue.size () >= (size_t) options.batch_size;
        });
        if (stopping) return;
        while (!queue.empty () &&
               batch.size () < (size_t) options.batch_size) {
          auto item= queue.front ();
          queue.pop_front ();
          ArtifactTask& task= item.first->tasks[item.second];
          if (task.state != TaskState::Queued) continue;
          if (item.first->cancel_requested || !item.first->error.empty ()) {
            task.state= TaskState::Cancelled;
            item.first->events.push_back (item.second);
            continue;
          }
          task.state= TaskState::Running;
          batch.push_back (std::move (item));
        }
      }
      if (batch.empty ()) { changed.notify_all (); continue; }

      std::vector<AthenaArtifactRangeRequest> requests;
      requests.reserve (batch.size ());
      for (const auto& item: batch)
        requests.push_back (item.first->tasks[item.second].request);
      std::atomic<size_t> completed {0};
      std::vector<std::vector<int>> selected=
        athena_artifact_select_definition_ranges (
          requests, options.model_path.string (), nullptr, &completed, true);

      // A model retry can be expensive.  Keep all inference outside the queue
      // mutex so submit/wait/cancel remain responsive while the worker runs.
      std::vector<bool> valid (batch.size (), false);
      selected.resize (batch.size ());
      for (size_t i=0; i<batch.size (); i++) {
        valid[i]= valid_offsets (requests[i], selected[i]);
        if (valid[i]) continue;
        auto retry= athena_artifact_select_definition_ranges (
          {requests[i]}, options.model_path.string (), nullptr, nullptr,
          true);
        if (retry.size () == 1 && valid_offsets (requests[i], retry[0])) {
          selected[i]= std::move (retry[0]);
          valid[i]= true;
        }
      }

      std::lock_guard<std::mutex> guard (mutex);
      for (size_t i=0; i<batch.size (); i++) {
        auto& job= batch[i].first;
        size_t task_index= batch[i].second;
        ArtifactTask& task= job->tasks[task_index];
        if (job->cancel_requested) task.state= TaskState::Cancelled;
        else if (!valid[i]) {
          task.state= TaskState::Failed;
          task.error= "definition-span model returned an invalid result";
          job->error= task.error;
        }
        else {
          task.offsets= std::move (selected[i]);
          task.state= TaskState::Completed;
        }
        job->events.push_back (task_index);
        job->last_access= Clock::now ();
      }
      athena_spdlog_info (
        "artifact delegation: completed microbatch requests=" +
        std::to_string (batch.size ()) +
        " remaining=" + std::to_string (queue.size ()));
      changed.notify_all ();
    }
  }

  ArtifactDelegationQueueOptions options;
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::map<std::string,std::shared_ptr<ArtifactJob>> jobs;
  std::map<std::string,std::string> submissions;
  std::deque<std::pair<std::shared_ptr<ArtifactJob>,size_t>> queue;
  size_t stored_items= 0;
  size_t stored_bytes= 0;
  bool stopping= false;
  std::thread worker;
};

ArtifactDelegationQueue::ArtifactDelegationQueue (
  ArtifactDelegationQueueOptions options)
  : impl (std::make_unique<Impl> (std::move (options))) {}

ArtifactDelegationQueue::~ArtifactDelegationQueue ()= default;

QJsonObject ArtifactDelegationQueue::submit (
  const std::string& principal, const QJsonObject& params) {
  return impl->submit (principal, params);
}

QJsonObject ArtifactDelegationQueue::wait (
  const std::string& principal, const QJsonObject& params) {
  return impl->wait (principal, params);
}

QJsonObject ArtifactDelegationQueue::cancel (
  const std::string& principal, const QJsonObject& params) {
  return impl->cancel (principal, params);
}

QJsonObject ArtifactDelegationQueue::acknowledge (
  const std::string& principal, const QJsonObject& params) {
  return impl->acknowledge (principal, params);
}

QJsonObject ArtifactDelegationQueue::counts () const { return impl->counts (); }
QJsonObject ArtifactDelegationQueue::limits () const { return impl->limits (); }
bool ArtifactDelegationQueue::available () const { return impl->available (); }

} // namespace athena::mcp
