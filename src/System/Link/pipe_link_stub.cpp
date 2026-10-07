/******************************************************************************
* MODULE     : pipe_link_stub.cpp
* DESCRIPTION: External-process pipe stubs for targets without process launch
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "tm_link.hpp"

namespace {

struct unavailable_pipe_link_rep final: tm_link_rep {
  unavailable_pipe_link_rep () { alive= false; }

  string start () override {
    return "Error: external processes are unavailable on this platform";
  }

  void write (string, int) override {}

  string& watch (int) override {
    static string empty= "";
    return empty;
  }

  string read (int) override { return ""; }
  void listen (int) override {}
  void interrupt () override {}
  void stop () override { alive= false; }
};

} // namespace

tm_link
make_pipe_link (string) {
  return tm_new<unavailable_pipe_link_rep> ();
}

void close_all_pipes () {}
void process_all_pipes () {}
