/******************************************************************************
* MODULE     : QTMVaultFontConfigurator.cpp
* DESCRIPTION: Transactional Vault-wide font configuration
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMVaultFontConfigurator.hpp"

#include "QTMFontSelector.hpp"
#include "ATHENA/Data/new_buffer.hpp"
#include "ATHENA/Data/vault.hpp"
#include "convert.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace fs= std::filesystem;

namespace {

struct FontRewrite {
  fs::path path;
  fs::path stage;
  fs::path backup;
  tree document;
};

std::string
tmStd (string value) {
  return std::string (as_charp (value), (size_t) N(value));
}

string
stdTm (const std::string& value) {
  return string (value.data (), (int) value.size ());
}

fs::path
normalizedPath (const fs::path& path) {
  std::error_code ec;
  fs::path absolute= fs::absolute (path, ec).lexically_normal ();
  fs::path canonical= fs::weakly_canonical (absolute, ec);
  return ec ? absolute : canonical;
}

bool
pathAtOrBelow (const fs::path& path, const fs::path& parent) {
  const fs::path normalizedPathValue= path.lexically_normal ();
  auto pathPart= normalizedPathValue.begin ();
  const fs::path normalizedParent= parent.lexically_normal ();
  for (auto parentPart= normalizedParent.begin ();
       parentPart != normalizedParent.end (); ++parentPart, ++pathPart)
    if (pathPart == normalizedPathValue.end () ||
        *pathPart != *parentPart)
      return false;
  return true;
}

std::string
operationId () {
  auto now= std::chrono::high_resolution_clock::now ().time_since_epoch ();
  return std::to_string (
    std::chrono::duration_cast<std::chrono::nanoseconds> (now).count ());
}

bool
fontPackage (tree style) {
  return is_atomic (style) && ends (style->label, "-font");
}

void
configureProgress (QProgressDialog& progress) {
  progress.setWindowTitle ("Configure Font for Vault");
  progress.setFixedWidth (600);
  progress.setWindowModality (Qt::ApplicationModal);
  progress.setMinimumDuration (0);
  QLabel* label= progress.findChild<QLabel*> ();
  if (label != nullptr) {
    label->setWordWrap (true);
    label->setMinimumWidth (0);
    label->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  }
}

void
removeStages (const std::vector<FontRewrite>& rewrites) {
  std::error_code ignored;
  for (const FontRewrite& rewrite: rewrites)
    fs::remove (rewrite.stage, ignored);
}

bool
readDocument (const fs::path& path, tree& document, QString& error) {
  string source;
  if (load_string (url_system (stdTm (path.string ())), source, false)) {
    error= QString ("Could not read %1").arg (
      QString::fromStdString (path.string ()));
    return false;
  }
  try { document= texmacs_document_to_tree (source); }
  catch (...) {
    error= QString ("Could not parse %1").arg (
      QString::fromStdString (path.string ()));
    return false;
  }
  if (is_func (document, _ERROR)) {
    error= QString ("Malformed ATHENA document: %1").arg (
      QString::fromStdString (path.string ()));
    return false;
  }
  return true;
}

bool
writeStage (FontRewrite& rewrite, QString& error) {
  string serialized= tree_to_texmacs (rewrite.document);
  if (save_string (url_system (stdTm (rewrite.stage.string ())),
                   serialized, false)) {
    error= QString ("Could not stage %1").arg (
      QString::fromStdString (rewrite.path.string ()));
    return false;
  }
  tree validation;
  try { validation= texmacs_document_to_tree (serialized); }
  catch (...) { validation= tree (_ERROR, "parse failed"); }
  if (is_func (validation, _ERROR)) {
    error= QString ("The rewritten document failed validation: %1").arg (
      QString::fromStdString (rewrite.path.string ()));
    return false;
  }
  std::error_code ec;
  fs::permissions (rewrite.stage, fs::status (rewrite.path, ec).permissions (), ec);
  return true;
}

bool
backupAll (const std::vector<FontRewrite>& rewrites, QString& error) {
  std::error_code ec;
  for (const FontRewrite& rewrite: rewrites) {
    fs::create_directories (rewrite.backup.parent_path (), ec);
    if (ec) {
      error= QString ("Could not create backup directory: %1")
        .arg (QString::fromStdString (ec.message ()));
      return false;
    }
    fs::copy_file (rewrite.path, rewrite.backup,
                   fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error= QString ("Could not back up %1: %2")
        .arg (QString::fromStdString (rewrite.path.string ()),
              QString::fromStdString (ec.message ()));
      return false;
    }
  }
  return true;
}

bool
installAll (const std::vector<FontRewrite>& rewrites, QString& error) {
  std::vector<const FontRewrite*> installed;
  std::error_code ec;
  for (const FontRewrite& rewrite: rewrites) {
    fs::rename (rewrite.stage, rewrite.path, ec);
    if (!ec) {
      installed.push_back (&rewrite);
      continue;
    }
    ec.clear ();
    fs::remove (rewrite.path, ec);
    ec.clear ();
    fs::rename (rewrite.stage, rewrite.path, ec);
    if (!ec) {
      installed.push_back (&rewrite);
      continue;
    }
    error= QString ("Could not install rewritten document %1: %2")
      .arg (QString::fromStdString (rewrite.path.string ()),
            QString::fromStdString (ec.message ()));
    std::error_code restoreCurrentError;
    fs::copy_file (rewrite.backup, rewrite.path,
                   fs::copy_options::overwrite_existing,
                   restoreCurrentError);
    for (const FontRewrite* done: installed) {
      std::error_code ignored;
      fs::copy_file (done->backup, done->path,
                     fs::copy_options::overwrite_existing, ignored);
    }
    return false;
  }
  return true;
}

} // namespace

tree
athena_document_with_font_profile (tree document, string profile) {
  tree initial= extract (document, "initial");
  tree rewrittenInitial (COLLECTION);
  for (int i=0; i<N(initial); ++i) {
    tree entry= initial[i];
    if (is_func (entry, ASSOCIATE, 2) && is_atomic (entry[0]) &&
        (entry[0]->label == "font" || entry[0]->label == "font-family"))
      continue;
    rewrittenInitial << entry;
  }
  rewrittenInitial << tree (ASSOCIATE, "font", profile)
                   << tree (ASSOCIATE, "font-family", "rm");
  document= change_doc_attr (document, "initial", rewrittenInitial);

  tree style= extract (document, "style");
  tree rewrittenStyle (TUPLE);
  for (int i=0; i<N(style); ++i)
    if (!fontPackage (style[i])) rewrittenStyle << style[i];
  return change_doc_attr (document, "style", rewrittenStyle);
}

void
qtm_configure_font_for_vault () {
  QWidget* parent= QApplication::activeWindow ();
  if (!vault_active ()) {
    QMessageBox::warning (parent, "Configure Font for Vault",
                          "Open a Vault before configuring its fonts.");
    return;
  }

  string selected;
  if (!native_font_profile_selector_dialog (
        get_preference ("vault preferred font", ""),
        "Configure Font for Vault", selected, parent))
    return;

  const fs::path root= normalizedPath (tmStd (concretize (vault_get_root ())));
  array<url> allFiles= vault_get_all_files ();
  std::vector<fs::path> files;
  files.reserve ((size_t) N(allFiles));
  for (int i=0; i<N(allFiles); ++i)
    if (suffix (allFiles[i]) == "ath") {
      fs::path source (tmStd (concretize (allFiles[i])));
      std::error_code ec;
      if (fs::is_symlink (fs::symlink_status (source, ec))) {
        QMessageBox::critical (
          parent, "Configure Font for Vault",
          QString ("Refusing to rewrite symbolic-link document: %1")
            .arg (QString::fromStdString (source.string ())));
        return;
      }
      fs::path path= normalizedPath (source);
      if (!pathAtOrBelow (path, root)) {
        QMessageBox::critical (
          parent, "Configure Font for Vault",
          QString ("Refusing to rewrite a document outside the Vault: %1")
            .arg (QString::fromStdString (path.string ())));
        return;
      }
      files.push_back (path);
    }
  std::sort (files.begin (), files.end ());
  files.erase (std::unique (files.begin (), files.end ()), files.end ());

  if (files.empty ()) {
    QMessageBox::information (parent, "Configure Font for Vault",
                              "The active Vault contains no .ath documents.");
    return;
  }

  std::unordered_map<std::string,url> openBuffers;
  QStringList modified;
  array<url> buffers= get_all_buffers ();
  for (int i=0; i<N(buffers); ++i) {
    if (is_rooted_tmfs (buffers[i]) || is_rooted_web (buffers[i])) continue;
    fs::path path= normalizedPath (tmStd (concretize (buffers[i])));
    if (!std::binary_search (files.begin (), files.end (), path)) continue;
    openBuffers[path.string ()]= buffers[i];
    if (buffer_modified (buffers[i]))
      modified << QString::fromStdString (path.filename ().string ());
  }
  if (!modified.isEmpty ()) {
    QMessageBox::warning (
      parent, "Configure Font for Vault",
      QString ("Save the affected open documents before continuing:\n\n%1")
        .arg (modified.join ("\n")));
    return;
  }

  QString profile= to_qstring (selected);
  QString summary= qtm_font_profile_summary (profile);
  if (QMessageBox::question (
        parent, "Configure Font for Vault",
        QString ("Apply %1 to %2 .ath document(s)?\n\n"
                 "The Vault preferred font will also be updated. Existing "
                 "font packages will be removed from those documents, and "
                 "the originals will be backed up before replacement.")
          .arg (summary).arg (files.size ()),
        QMessageBox::Apply | QMessageBox::Cancel,
        QMessageBox::Cancel) != QMessageBox::Apply)
    return;

  const std::string id= operationId ();
  const fs::path backupRoot= root / ".backup" / "font-configuration" / id;
  std::vector<FontRewrite> rewrites;
  rewrites.reserve (files.size ());
  QProgressDialog progress ("Preparing Vault font configuration...", "Cancel",
                            0, (int) files.size (), parent);
  configureProgress (progress);

  QString error;
  for (size_t i=0; i<files.size (); ++i) {
    const fs::path& path= files[i];
    progress.setValue ((int) i);
    progress.setLabelText (
      QString ("Preparing %1 of %2: %3")
        .arg (i + 1).arg (files.size ())
        .arg (QString::fromStdString (path.filename ().string ())));
    QApplication::processEvents ();
    if (progress.wasCanceled ()) {
      removeStages (rewrites);
      return;
    }
    tree document;
    if (!readDocument (path, document, error)) {
      removeStages (rewrites);
      QMessageBox::critical (parent, "Configure Font for Vault", error);
      return;
    }
    FontRewrite rewrite;
    rewrite.path= path;
    rewrite.stage= path;
    rewrite.stage += ".athena-font-" + id + ".tmp";
    rewrite.backup= backupRoot / path.lexically_relative (root);
    rewrite.document= athena_document_with_font_profile (document, selected);
    if (!writeStage (rewrite, error)) {
      rewrites.push_back (rewrite);
      removeStages (rewrites);
      QMessageBox::critical (parent, "Configure Font for Vault", error);
      return;
    }
    rewrites.push_back (std::move (rewrite));
  }

  progress.setCancelButton (nullptr);
  progress.setRange (0, 0);
  progress.setLabelText ("Backing up and installing configured documents...");
  QApplication::processEvents ();
  if (!backupAll (rewrites, error) || !installAll (rewrites, error)) {
    removeStages (rewrites);
    QMessageBox::critical (parent, "Configure Font for Vault", error);
    return;
  }
  progress.setRange (0, 1);
  progress.setValue (1);

  set_preference ("vault preferred font", selected);
  for (const FontRewrite& rewrite: rewrites) {
    auto open= openBuffers.find (rewrite.path.string ());
    if (open != openBuffers.end ()) set_buffer_tree (open->second, rewrite.document);
  }
  QMessageBox::information (
    parent, "Configure Font for Vault",
    QString ("Configured %1 document(s). Backups are stored in:\n%2")
      .arg (rewrites.size ())
      .arg (QString::fromStdString (backupRoot.string ())));
}
