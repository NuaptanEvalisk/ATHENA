/******************************************************************************
* MODULE     : example.cpp
* DESCRIPTION: Standalone AUDMAP SDK selector and lineage example
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include <athena/audmap/client.hpp>
#include <iostream>
int main (int argc, char** argv) {
  try {
    athena::audmap::options config;
    if (argc > 1) config.endpoint = argv[1];
    if (argc > 2) config.identity = argv[2];
    athena::audmap::client client (config);
    auto request = client.resolve (argc > 3 ? argv[3] : "@/vaults/@", true);
    auto resolved = request.result.get ();
    std::cout << resolved.data.dump (2) << std::endl;
    for (const auto& handle: resolved.data.at (0)) {
      auto lineage = client.lineage (request.ticket, handle.get<athena::audmap::id> ());
      std::cout << client.ask (lineage.ticket, lineage.operation).result.get ().data.dump () << std::endl;
      client.release (lineage.ticket, lineage.operation).result.get ();
    }
    client.finish (request.ticket).result.get ();
  }
  catch (const std::exception& e) { std::cerr << e.what () << std::endl; return 1; }
}
