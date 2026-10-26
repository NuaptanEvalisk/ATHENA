#ifndef ATHENA_NODE_GROUPS_HPP
#define ATHENA_NODE_GROUPS_HPP

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace athena::contracts {

// Immutable, ordered semantic groups. No editor, Scheme, or tree ownership.
class node_groups {
  struct entry { std::string name; bool group; };
  std::map<std::string, std::vector<entry>, std::less<>> declarations_;
  std::map<std::string, std::vector<std::string>, std::less<>> members_;
  std::map<std::string, std::set<std::string>, std::less<>> dependencies_;
  std::map<std::string, std::string, std::less<>> alternates_;
  void resolve (const std::string&, std::vector<std::string>&);

public:
  explicit node_groups (std::string_view json);
  const std::vector<std::string>& members (std::string_view group) const;
  bool contains (std::string_view group, std::string_view tag) const;
  bool includes (std::string_view group, std::string_view subgroup) const;
  std::string find (std::string_view group, std::string_view tag) const;
  std::string alternate (std::string_view tag) const;
  std::vector<std::string> variants (std::string_view tag, bool numbered) const;
};

const node_groups& standard_node_groups ();

} // namespace athena::contracts
#endif
