/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#include "revisions.hpp"
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace athena::hodarium;
static void require (bool condition, const char* message) {
  if (!condition) throw std::runtime_error (message);
}
int main () {
  try {
    QTemporaryDir directory;
    require (directory.isValid (), "Cannot create collision fixture");
    revision_store store (std::filesystem::path (directory.path ().toStdString ()) / "journal.sqlite");
    auto add= [&] (std::string object, std::string path, std::string format= "ath-xml-v2",
                  std::vector<std::string> parents= {}, bool deleted= false,
                  std::string vault= "vault") {
      auto value= seal_revision ({ {}, vault, object, "test-member", format, 3,
        path, deleted, parents, deleted ? "" : "payload must not enter metadata listing" });
      store.receive (value); return value;
    };
    auto first= add ("object-a", "a.ath");
    auto second= add ("object-b", "a.ath");
    add ("resource-a", "b.png", "ath-resource-blob-v1");
    add ("resource-b", "b.png", "ath-resource-blob-v1");
    add ("other-vault", "a.ath", "ath-xml-v2", {}, false, "other");
    add ("logical-a", "a.ath", "athena-namespace-v1");
    add ("logical-b", "virtual-history", "athena-namespace-v1");
    add ("logical-c", "virtual-history", "athena-artifact-rejections-v1");
    auto obsolete= add ("deleted-object", "a.ath");
    add (obsolete.object, obsolete.relative_path, obsolete.format, {obsolete.id}, true);
    auto branching= add ("one-object", "single.ath");
    auto left= add (branching.object, "single.ath", branching.format, {branching.id});
    auto right= add (branching.object, "single.ath", "ath-resource-avd-v1", {branching.id});
    auto unapproved= add (branching.object, "a.ath", branching.format, {left.id,right.id});
    add (branching.object, "b.png", branching.format, {unapproved.id});
    auto page= store.path_collisions ("vault", {}, 1);
    require (page.size () == 1 && page[0].path == "a.ath" && page[0].heads.size () == 2,
             "First path page includes tombstone, logical, ineligible or foreign heads");
    require (page[0].heads[0].object == first.object && page[0].heads[1].object == second.object,
             "Collision metadata ordering is unstable");
    for (const auto& head: page[0].heads)
      require (head.payload.empty () && !head.id.empty () && head.relative_path == page[0].path,
               "Collision listing is not complete metadata-only output");
    page= store.path_collisions ("vault", page[0].path, 1);
    require (page.size () == 1 && page[0].path == "b.png" && page[0].heads.size () == 2,
             "Resource collision or path cursor missing");
    require (store.path_collisions ("vault", page[0].path).empty () &&
             store.path_collisions ("other").empty (), "Pagination or Vault scope failed");
    add (second.object, "renamed.ath", second.format, {second.id});
    page= store.path_collisions ("vault");
    require (page.size () == 1 && page[0].path == "b.png", "Retired head remains a path collision");
    bool rejected= false;
    try {store.path_collisions ("vault", {}, 0);} catch (const std::invalid_argument&) {rejected= true;}
    require (rejected,"Unbounded collision page accepted");
    std::cout << "Hodarium metadata-only path collision query passed\n";
  } catch (const std::exception& error) {std::cerr << error.what () << '\n'; return 1;}
}
