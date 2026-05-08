#ifndef AOFM_METADATA_H
#define AOFM_METADATA_H

#include <string>

namespace aofm {

struct AofmDocumentMetadata {
  std::string created_time;
  std::string modified_time;
  std::string content_hash;

  void clear() {
    created_time.clear();
    modified_time.clear();
    content_hash.clear();
  }
};

extern AofmDocumentMetadata aofm_metadata;

void set_aofm_metadata_field(const std::string& key, const std::string& value);
void parse_aofm_metadata_line(const std::string& line);
bool is_aofm_metadata_header_line(const std::string& line);
bool is_aofm_metadata_line(const std::string& line);
std::string extract_aofm_document_metadata(const std::string& raw);

} // namespace aofm

#endif
