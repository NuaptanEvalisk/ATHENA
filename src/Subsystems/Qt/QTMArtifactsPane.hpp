/******************************************************************************
* MODULE     : QTMArtifactsPane.hpp
* DESCRIPTION: Artifact browser and build commands
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#ifndef QTMARTIFACTSPANE_HPP
#define QTMARTIFACTSPANE_HPP

#include "ATHENA/Data/artifacts.hpp"
#include "url.hpp"

#include <QWidget>
#include <vector>

class QComboBox;
class QLineEdit;
class QTableWidget;
class QTimer;

class QTMArtifactsPane: public QWidget {
public:
  explicit QTMArtifactsPane (QWidget* parent= nullptr);
  void refresh ();

private:
  void refreshNamespaces ();
  void applyFilter ();
  void openRow (int row);
  QString currentRelativePath () const;

  QComboBox* scope;
  QComboBox* namespaceSelector;
  QLineEdit* search;
  QTableWidget* table;
  QTimer* currentWatcher;
  QString lastCurrentPath;
  std::vector<AthenaArtifactRecord> records;
};

void artifacts_pane_show ();
void artifacts_build_entire_vault ();
void artifacts_build_current_document ();
bool artifacts_open_uuid (string uuid);
bool artifacts_resolve_uuid (string uuid, url& file, path& source_path);

#endif // QTMARTIFACTSPANE_HPP
