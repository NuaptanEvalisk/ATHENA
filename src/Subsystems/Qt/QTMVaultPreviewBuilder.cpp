/******************************************************************************
* MODULE     : QTMVaultPreviewBuilder.cpp
* DESCRIPTION: Vault preview tree builders
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultPreviewBuilder.hpp"
#include "convert.hpp"
#include "converter.hpp"
#include "new_buffer.hpp"
#include "scheme.hpp"
#include <algorithm>

tree
apply_vault_preferred_font_to_preview (tree body) {
  string font= get_preference ("vault preferred font", "");
  if (font == "") return body;
  return tree (WITH, "font", font, body);
}

tree
import_body (url file) {
  tree t= import_tree (file, "texmacs");
  tree body= extract (t, "body");
  return is_empty (body) ? t : body;
}

static bool
preview_absolute_image_path (const string& path) {
  return path == "" || starts (path, "/") || starts (path, "~") ||
    starts (path, "$") || occurs ("://", path);
}

static string
preview_rebase_image_path (const string& path, url sourceDir) {
  if (preview_absolute_image_path (path)) return path;
  url absolute= sourceDir * url_unix (cork_to_utf8 (path));
  return utf8_to_cork (as_system_string (absolute));
}

static tree
rebase_preview_images (tree t, url sourceDir) {
  if (is_atomic (t)) return copy (t);

  tree r (L(t));
  for (int i=0; i<N(t); i++) {
    if (i == 0 && is_func (t, IMAGE) && is_atomic (t[i]))
      r << tree (preview_rebase_image_path (t[i]->label, sourceDir));
    else
      r << rebase_preview_images (t[i], sourceDir);
  }
  return r;
}

tree
import_body_for_preview (url file) {
  return rebase_preview_images (import_body (file), head (file));
}

static int
path_top_index (path p) {
  if (is_nil (p)) return 0;
  return p->item;
}

tree
build_preview_from_body (tree body, path focus, int* firstOut,
                         int* lastOut) {
  if (firstOut != nullptr) *firstOut= 0;
  if (lastOut != nullptr) *lastOut= 0;
  if (is_empty (body)) return tree (DOCUMENT, "");

  if (!is_func (body, DOCUMENT)) {
    if (firstOut != nullptr) *firstOut= 0;
    if (lastOut != nullptr) *lastOut= 1;
    return tree (DOCUMENT, compound ("marked", copy (body)));
  }

  if (N(body) == 0) return tree (DOCUMENT, "");

  int top= path_top_index (focus);
  if (top < 0 || top >= N(body)) top= 0;
  int first= std::max (0, top - 2);
  int last = std::min (N(body), top + 3);
  if (firstOut != nullptr) *firstOut= first;
  if (lastOut != nullptr) *lastOut= last;

  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    tree block= copy (body[i]);
    if (i == top) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}

tree
build_preview_from_anchor_range (tree body, path upper, path lower,
                                 int* firstOut, int* lastOut, bool detached) {
  if (firstOut != nullptr) *firstOut= 0;
  if (lastOut != nullptr) *lastOut= 0;
  if (is_empty (body)) return tree (DOCUMENT, "");

  if (!is_func (body, DOCUMENT)) {
    if (firstOut != nullptr) *firstOut= 0;
    if (lastOut != nullptr) *lastOut= 1;
    return tree (DOCUMENT, compound ("marked", copy (body)));
  }

  if (N(body) == 0) return tree (DOCUMENT, "");
  int first= path_top_index (upper);
  int last = path_top_index (lower);
  if (first < 0 || first >= N(body)) first= 0;
  if (last < first || last >= N(body)) last= first;
  last++;
  if (firstOut != nullptr) *firstOut= first;
  if (lastOut != nullptr) *lastOut= last;

  tree preview (DOCUMENT);
  for (int i= first; i<last; i++) {
    // Search only reads this range in the source tree's owning worker.
    tree block= detached ? copy (body[i]) : body[i];
    if (i == first) block= compound ("marked", block);
    preview << block;
  }
  return preview;
}
