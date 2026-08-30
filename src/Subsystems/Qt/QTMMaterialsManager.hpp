/******************************************************************************
* MODULE     : QTMMaterialsManager.hpp
* DESCRIPTION: Vault Materials manager and landing pad
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMMATERIALSMANAGER_HPP
#define QTMMATERIALSMANAGER_HPP

#include "ATHENA/Data/materials.hpp"
#include "ATHENA/Data/materials_schema.hpp"

#include <QStringList>
#include <QWidget>
#include <vector>

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
struct MaterialRecognitionResult;

class QTMMaterialsManager: public QWidget {
public:
  explicit QTMMaterialsManager (QWidget* parent= nullptr);

  void refresh ();

protected:
  void dragEnterEvent (QDragEnterEvent* event) override;
  void dropEvent (QDropEvent* event) override;

private:
  void rebuildList ();
  void loadSelection ();
  void clearEditor ();
  bool saveEditor ();
  void createEmpty ();
  void removeSelected ();
  void reidentifySelected ();
  void chooseFiles ();
  void chooseDirectory ();
  void importBibtex ();
  void importZotero ();
  void canonicalizeFilenames ();
  void importFiles (const QStringList& files, bool review_recognition);
  bool reviewRecognition (const QString& path,
                          MaterialRecognitionResult& recognition,
                          const QString& accept_text= "Add Material");
  void openAttachment (int row);
  void populateTypeCombo (QComboBox* combo, const std::string& selected);
  void addCreatorRow (const MaterialCreator* creator= nullptr);
  void addFieldRow (const MaterialField* field= nullptr);
  std::string creatorRoleAt (int row) const;
  std::string fieldNameAt (int row) const;
  QString selectedUuid () const;
  QStringList selectedUuids () const;
  void selectUuid (const QString& uuid);

  QLineEdit* searchEdit;
  QTableWidget* materialTable;
  QLabel* landingPad;
  QComboBox* typeEdit;
  QLineEdit* titleEdit;
  QLineEdit* dateEdit;
  QLineEdit* containerEdit;
  QLineEdit* publisherEdit;
  QLineEdit* tagsEdit;
  QTableWidget* creatorTable;
  QTableWidget* identifierTable;
  QTableWidget* fieldTable;
  QTableWidget* attachmentTable;
  QLabel* stateLabel;
  QPushButton* saveButton;
  QPushButton* deleteButton;
  QPushButton* reidentifyButton;
  std::vector<MaterialSearchHit> records;
  MaterialRecord loaded;
  MaterialSchema schema;
};

void materials_manager_show ();

#endif // QTMMATERIALSMANAGER_HPP
