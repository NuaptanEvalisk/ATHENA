/******************************************************************************
* MODULE     : artifact_delegation_queue.hpp
* DESCRIPTION: In-memory FIFO for delegated artifact definition spans
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
*/

#ifndef ARTIFACT_DELEGATION_QUEUE_HPP
#define ARTIFACT_DELEGATION_QUEUE_HPP

#include <QJsonObject>

#include <filesystem>
#include <memory>
#include <string>

namespace athena::mcp {

struct ArtifactDelegationQueueOptions {
  std::filesystem::path model_path;
  int batch_size= 16;
  int max_requests_per_job= 512;
  int max_plaintext_bytes= 8 * 1024 * 1024;
  int max_queued_items= 4096;
  int max_stored_bytes= 32 * 1024 * 1024;
  int result_retention_seconds= 15 * 60;
};

class ArtifactDelegationQueue {
public:
  explicit ArtifactDelegationQueue (ArtifactDelegationQueueOptions options);
  ~ArtifactDelegationQueue ();

  QJsonObject submit (const std::string& principal,
                      const QJsonObject& params);
  QJsonObject wait (const std::string& principal,
                    const QJsonObject& params);
  QJsonObject cancel (const std::string& principal,
                      const QJsonObject& params);
  QJsonObject acknowledge (const std::string& principal,
                           const QJsonObject& params);
  QJsonObject counts () const;

  QJsonObject limits () const;
  bool available () const;

private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace athena::mcp

#endif // ARTIFACT_DELEGATION_QUEUE_HPP
