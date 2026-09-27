/******************************************************************************
* MODULE     : QTMNodePropertiesDialog.hpp
* DESCRIPTION: Native node property inspector and actor-owned edit handoff
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <functional>
#include <string>

class QDialog;
class QWidget;
class tree;

using node_property_completion= std::function<void (std::string)>;
using node_property_submit=
  std::function<void (std::string, node_property_completion)>;

// UI-thread factory. The snapshot is an XML v2 body-free metadata header.
// Completion is delivered on the UI thread: empty means success, else error.
QDialog* make_node_properties_dialog (
  std::string snapshot, bool canonical, bool writable,
  node_property_submit submit, QWidget* parent= nullptr);

// Called from the owning BufferActor, with the actual attached focus node.
bool node_properties_show (tree source);
