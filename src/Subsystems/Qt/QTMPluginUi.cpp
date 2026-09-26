/******************************************************************************
* MODULE     : QTMPluginUi.cpp
* DESCRIPTION: Plugin installation, permissions, startup settings and lifecycle controls
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMPluginUi.hpp"
#include "QTMPluginManager.hpp"
#include "QTMToast.hpp"
#include "QTMAudmap.hpp"
#include "QTMVaultPreviewWidget.hpp"
#include "athena_document_xml.hpp"
#include "convert.hpp"
#include "unicode_text.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <fstream>
#include <set>

namespace {
QString qs (const std::string& text) { return QString::fromUtf8 (text.data (), text.size ()); }
QString menu_text (const std::string& text) { return qs (text).replace ('&', "&&"); }
void execute (QWidget* parent, const std::function<void ()>& action) {
  try { action (); }
  catch (const std::exception& e) { QMessageBox::warning (parent, "ATHENA Plugins", QString::fromUtf8 (e.what ())); }
}
void launch_plugin (QTMPluginManager* manager, const std::string& id, bool restart) {
  try {
    if (restart) manager->restart (id, true);
    else manager->start (id, true);
  }
  catch (const std::exception& e) {
    qtm_show_toast (string (e.what ()), "Plugin Failed to Start");
  }
}
QString jail_label (const athena::plugins::jail_permission& permission) {
  using namespace athena::plugins;
  if (permission.permission == jail_permission_kind::network)
    return "Network (Internet, LAN and localhost)";
  const QString path= permission.path.empty () ? QStringLiteral ("/") :
    QStringLiteral ("/") + qs (permission.path.generic_string ());
  const QString mode= permission.permission == jail_permission_kind::filesystem_read ?
    "Read current vault " : "Read & write current vault ";
  return mode + path + (permission.scope == filesystem_scope::tree ? " (tree)" : " (file)");
}
void fill_permissions (const athena::plugins::manifest& manifest, const QTMPluginPolicy& policy,
                       QTreeWidget* jail, QTreeWidget* audmap, bool default_required) {
  jail->clear (); audmap->clear ();
  for (const auto& request: manifest.jail_permissions) {
    const auto id= athena::plugins::jail_permission_id (request);
    auto* item= new QTreeWidgetItem (jail, {jail_label (request), request.required ? "Required" : "Optional"});
    item->setData (0, Qt::UserRole, qs (id));
    item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
    item->setCheckState (0, policy.jailGrants.count (id) || (default_required && request.required) ?
                            Qt::Checked : Qt::Unchecked);
  }
  for (const auto& request: manifest.audmap_permissions) {
    for (const auto& action: request.actions) {
      auto* item= new QTreeWidgetItem (audmap,
        {qs (request.resource + ": " + action), request.required ? "Required" : "Optional"});
      item->setData (0, Qt::UserRole, qs (request.resource));
      item->setData (0, Qt::UserRole + 1, qs (action));
      item->setFlags (item->flags () | Qt::ItemIsUserCheckable);
      auto found= policy.audmapGrants.find (request.resource);
      item->setCheckState (0,
        (found != policy.audmapGrants.end () && found->second.count (action)) ||
        (default_required && request.required) ? Qt::Checked : Qt::Unchecked);
    }
  }
  jail->resizeColumnToContents (1); audmap->resizeColumnToContents (1);
}
QTMPluginPolicy policy_from_permissions (QTreeWidget* jail, QTreeWidget* audmap,
                                         QTMPluginPolicy policy) {
  policy.jailGrants.clear (); policy.audmapGrants.clear ();
  for (int i=0; i<jail->topLevelItemCount (); ++i) {
    auto* item= jail->topLevelItem (i);
    if (item->checkState (0) == Qt::Checked)
      policy.jailGrants.insert (item->data (0, Qt::UserRole).toString ().toStdString ());
  }
  for (int i=0; i<audmap->topLevelItemCount (); ++i) {
    auto* item= audmap->topLevelItem (i);
    if (item->checkState (0) != Qt::Checked) continue;
    policy.audmapGrants[item->data (0, Qt::UserRole).toString ().toStdString ()].insert (
      item->data (0, Qt::UserRole + 1).toString ().toStdString ());
  }
  return policy;
}

bool safe_license_tree (tree t) {
  if (is_atomic (t)) return true;
  // Licenses are rendered before installation, so only a deliberately small
  // static-document vocabulary is accepted. Unknown/custom macros are rejected
  // rather than interpreted by the normal typesetter.
  static const std::set<std::string> allowed {
    "document", "concat", "para", "surround", "with", "rigid",
    "hspace", "vspace", "space", "line-break", "new-line", "page-break",
    "strong", "em", "verbatim", "code", "tt", "small", "large",
    "section", "subsection", "subsubsection", "paragraph", "subparagraph",
    "itemize", "enumerate", "description", "item", "item*",
    "table", "row", "cell", "tabular", "block",
    "math", "frac", "sqrt", "root", "rsub", "rsup", "lsub", "lsup",
    "around", "around*", "left", "mid", "right", "wide", "neg",
    "matrix", "det", "binom", "choice", "above", "below"
  };
  const string tm_label= as_string (L (t));
  const std::string label (as_charp (tm_label), static_cast<std::size_t> (N(tm_label)));
  if (!allowed.count (label)) return false;
  for (int i=0; i<N(t); ++i) if (!safe_license_tree (t[i])) return false;
  return true;
}

tree read_license_ath (const std::filesystem::path& path) {
  std::ifstream input (path, std::ios::binary);
  if (!input) throw std::runtime_error ("Cannot read plugin license");
  std::string bytes ((std::istreambuf_iterator<char> (input)), std::istreambuf_iterator<char> ());
  tree document= athena::document::read_xml (bytes, athena::document::xml_kind::document);
  if (!is_func (document, DOCUMENT)) throw std::runtime_error ("Plugin ATHENA license is not a document");
  tree body= extract (document, "body");
  if (!safe_license_tree (body))
    throw std::runtime_error ("Plugin ATHENA license contains unsupported or active content");
  return body;
}

bool review_install (QTMPluginManager* manager, QWidget* parent) {
  const auto pending= manager->pendingInstall ();
  if (!pending) return false;
  QDialog dialog (parent); dialog.setWindowTitle ("Review ATHENA Plugin"); dialog.resize (760, 760);
  auto* layout= new QVBoxLayout (&dialog);
  auto* title= new QLabel ("<b>" + qs (pending->manifest.name).toHtmlEscaped () + "</b><br>" +
                           qs (pending->manifest.description).toHtmlEscaped (), &dialog);
  title->setWordWrap (true); layout->addWidget (title);
  auto* tabs= new QTabWidget (&dialog); layout->addWidget (tabs, 1);

  std::unique_ptr<WikilinkPreview> preview;
  if (pending->licenseFile) {
    auto* page= new QWidget; auto* pageLayout= new QVBoxLayout (page);
    try {
      if (pending->manifest.license->format == athena::plugins::license_format::text) {
        QFile file (QString::fromStdString (pending->licenseFile->string ()));
        if (!file.open (QIODevice::ReadOnly) || file.size () > 4 * 1024 * 1024)
          throw std::runtime_error ("Cannot read plugin text license");
        const QByteArray bytes= file.readAll ();
        if (!athena::text::valid_utf8 (std::string_view (bytes.constData (), bytes.size ())))
          throw std::runtime_error ("Plugin text license is not valid UTF-8");
        auto* text= new QPlainTextEdit (QString::fromUtf8 (bytes), page);
        text->setReadOnly (true); pageLayout->addWidget (text);
      }
      else {
        auto* host= new QWidget (page); host->setMinimumHeight (420);
        new QVBoxLayout (host); pageLayout->addWidget (host, 1);
        preview= std::make_unique<WikilinkPreview> (&dialog);
        preview->setBody (read_license_ath (*pending->licenseFile));
        preview->ensureCreated (host);
      }
    }
    catch (const std::exception& e) {
      manager->cancelInstall ();
      QMessageBox::warning (parent, "Invalid Plugin License", QString::fromUtf8 (e.what ()));
      return false;
    }
    tabs->addTab (page, "License");
  }

  auto* permissions= new QWidget; auto* permissionsLayout= new QVBoxLayout (permissions);
  permissionsLayout->addWidget (new QLabel ("System access (Minijail)", permissions));
  auto* jail= new QTreeWidget (permissions); jail->setHeaderLabels ({"Permission", "Request"});
  permissionsLayout->addWidget (jail, 1);
  permissionsLayout->addWidget (new QLabel ("ATHENA access (AUDMAP)", permissions));
  auto* audmap= new QTreeWidget (permissions); audmap->setHeaderLabels ({"Permission", "Request"});
  permissionsLayout->addWidget (audmap, 1);
  QTMPluginPolicy proposed; fill_permissions (pending->manifest, proposed, jail, audmap, true);
  tabs->addTab (permissions, "Permissions");

  auto* buttons= new QDialogButtonBox (&dialog);
  auto* accept= buttons->addButton ("Accept && Install", QDialogButtonBox::AcceptRole);
  buttons->addButton (QDialogButtonBox::Cancel);
  QObject::connect (accept, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget (buttons);
  if (dialog.exec () != QDialog::Accepted) { manager->cancelInstall (); return false; }
  manager->acceptInstall (policy_from_permissions (jail, audmap, proposed));
  return true;
}
QToolButton* tool (QWidget* parent, QHBoxLayout* layout, const char* icon, const char* text,
                  QStyle::StandardPixmap fallback = QStyle::SP_CustomBase) {
  auto* button = new QToolButton (parent);
  button->setIcon (QIcon::fromTheme (icon, fallback == QStyle::SP_CustomBase ? QIcon () : parent->style ()->standardIcon (fallback)));
  button->setToolTip (text); button->setAccessibleName (text);
  layout->addWidget (button); return button;
}
void update_text (QPlainTextEdit* editor, const QString& text) {
  if (editor->toPlainText () == text) return;
  const auto position = editor->verticalScrollBar ()->value ();
  const bool bottom = position == editor->verticalScrollBar ()->maximum ();
  editor->setPlainText (text);
  editor->verticalScrollBar ()->setValue (bottom ? editor->verticalScrollBar ()->maximum () : position);
}
class plugin_page final: public QWidget {
  QPointer<QTMPluginManager> manager;
  QTreeWidget* list;
  QWidget* settings;
  QComboBox *startup, *trust;
  QSpinBox* delay;
  QTreeWidget *jailRules, *audmapRules;
  QToolButton *remove, *startStop, *restart, *force, *install;
  QPushButton* apply;
  QProgressBar* progress;
  QLabel *identity, *status, *message;
  QPlainTextEdit *log, *result;
  std::string selected;
  QTMPluginInfo current;
  bool awaitingInstall = false;
  void load_policy () {
    identity->setText (qs (current.manifest.id));
    startup->setCurrentIndex (static_cast<int> (current.policy.startup));
    trust->setCurrentIndex (static_cast<int> (current.policy.trust));
    delay->setValue (current.policy.delaySeconds);
    fill_permissions (current.manifest, current.policy, jailRules, audmapRules, false);
  }
  void refresh () {
    if (!manager) { setEnabled (false); return; }
    const auto plugins = manager->plugins ();
    const auto previous = selected;
    {
      QSignalBlocker blocked (list);
      list->clear ();
      QTreeWidgetItem* chosen = nullptr;
      for (const auto& plugin: plugins) {
        auto* item = new QTreeWidgetItem (list, {qs (plugin.manifest.name), qs (plugin.manifest.version), plugin.state});
        item->setData (0, Qt::UserRole, qs (plugin.manifest.id));
        item->setToolTip (0, qs (plugin.manifest.id));
        if (plugin.manifest.id == selected) chosen = item;
      }
      if (!chosen && list->topLevelItemCount ()) chosen = list->topLevelItem (0);
      if (chosen) { list->setCurrentItem (chosen); selected = chosen->data (0, Qt::UserRole).toString ().toStdString (); }
      else selected.clear ();
    }
    bool found = false;
    for (const auto& plugin: plugins) if (plugin.manifest.id == selected) { current = plugin; found = true; break; }
    const bool busy = manager->busy ();
    progress->setVisible (busy); install->setEnabled (!busy);
    settings->setEnabled (found && !busy);
    remove->setEnabled (found && !busy && !current.running);
    startStop->setEnabled (found && !busy && current.state != "Invalid");
    restart->setEnabled (found && !busy && current.state != "Invalid");
    force->setEnabled (found && current.running);
    if (!found) { selected.clear (); return; }
    if (previous != selected) load_policy ();
    const bool active = current.running || current.state == "Scheduled";
    startStop->setIcon (QIcon::fromTheme (active ? "media-playback-stop" : "media-playback-start",
      style ()->standardIcon (active ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay)));
    startStop->setToolTip (active ? "Stop plugin" : "Start plugin");
    startStop->setAccessibleName (startStop->toolTip ());
    status->setText (current.state + (current.running ? (current.connected ? " (connected)" : " (not connected)") : "") +
      (current.error.isEmpty () ? "" : "\n" + current.error));
    apply->setText (current.running ? "Apply and stop" : "Apply");
    update_text (log, current.log);
    update_text (result, current.lastResult.is_null () ? QString () : qs (current.lastResult.dump (2)));
  }
  void install_package (bool directory) {
    const auto path = directory ? QFileDialog::getExistingDirectory (this, "Install plugin directory") :
      QFileDialog::getOpenFileName (this, "Install plugin ZIP", {}, "ZIP packages (*.zip)");
    if (path.isEmpty ()) return;
    execute (this, [&] {
      awaitingInstall= true;
      try { manager->install (path.toStdString ()); }
      catch (...) { awaitingInstall= false; throw; }
    });
  }
  void save () {
    QTMPluginPolicy policy;
    policy.startup = static_cast<QTMPluginPolicy::Startup> (startup->currentIndex ());
    policy.trust = static_cast<athena::interop::trust_mode> (trust->currentIndex ());
    policy.delaySeconds = delay->value ();
    policy= policy_from_permissions (jailRules, audmapRules, std::move (policy));
    manager->configure (selected, policy);
    message->setText ("Settings saved.");
  }
public:
  plugin_page (QTMPluginManager* manager, QWidget* parent): QWidget (parent), manager (manager) {
    setObjectName ("athena-plugin-preferences");
    auto* layout = new QVBoxLayout (this);
    auto* toolbar = new QHBoxLayout;
    install = tool (this, toolbar, "document-import", "Install plugin", QStyle::SP_DialogOpenButton);
    install->setObjectName ("plugin-install");
    install->setPopupMode (QToolButton::InstantPopup);
    auto* importMenu = new QMenu (install);
    importMenu->addAction ("From directory...", this, [this] { install_package (true); });
    importMenu->addAction ("From ZIP...", this, [this] { install_package (false); });
    install->setMenu (importMenu);
    remove = tool (this, toolbar, "edit-delete", "Uninstall plugin", QStyle::SP_TrashIcon);
    startStop = tool (this, toolbar, "media-playback-start", "Start plugin", QStyle::SP_MediaPlay);
    restart = tool (this, toolbar, "view-refresh", "Restart plugin", QStyle::SP_BrowserReload);
    force = tool (this, toolbar, "process-stop", "Force quit plugin", QStyle::SP_DialogCloseButton);
    startStop->setObjectName ("plugin-start-stop"); restart->setObjectName ("plugin-restart");
    force->setObjectName ("plugin-force-quit"); remove->setObjectName ("plugin-uninstall");
    toolbar->addStretch (); layout->addLayout (toolbar);
    progress = new QProgressBar; progress->setRange (0, 0); progress->setTextVisible (false); layout->addWidget (progress);
    list = new QTreeWidget; list->setObjectName ("plugin-list"); list->setHeaderLabels ({"Plugin", "Version", "State"});
    list->setRootIsDecorated (false); list->setUniformRowHeights (true); list->setMinimumHeight (120);
    list->header ()->setSectionResizeMode (0, QHeaderView::Stretch); layout->addWidget (list, 1);
    settings = new QWidget; auto* form = new QFormLayout (settings); layout->addWidget (settings);
    identity = new QLabel; identity->setTextFormat (Qt::PlainText); identity->setWordWrap (true);
    form->addRow ("Plugin ID", identity);
    status = new QLabel; status->setObjectName ("plugin-status"); status->setTextFormat (Qt::PlainText); status->setWordWrap (true);
    form->addRow ("Status", status);
    startup = new QComboBox; startup->setObjectName ("plugin-startup"); startup->addItems ({"Manual", "Automatic", "Delayed"});
    form->addRow ("Startup", startup);
    delay = new QSpinBox; delay->setObjectName ("plugin-startup-delay"); delay->setRange (1, 86400); delay->setSuffix (" s");
    form->addRow ("Startup delay", delay);
    trust = new QComboBox; trust->setObjectName ("plugin-trust");
    trust->addItems ({"No confirmation", "Confirm operations", "Confirm every request"}); form->addRow ("Confirmation", trust);
    jailRules= new QTreeWidget; jailRules->setObjectName ("plugin-jail-permissions");
    jailRules->setHeaderLabels ({"System permission", "Request"});
    jailRules->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
    jailRules->setRootIsDecorated (false); jailRules->setMaximumHeight (150);
    form->addRow ("System access", jailRules);
    audmapRules= new QTreeWidget; audmapRules->setObjectName ("plugin-audmap-permissions");
    audmapRules->setHeaderLabels ({"ATHENA permission", "Request"});
    audmapRules->header ()->setSectionResizeMode (0, QHeaderView::Stretch);
    audmapRules->setRootIsDecorated (false); audmapRules->setMaximumHeight (180);
    form->addRow ("ATHENA access", audmapRules);
    apply = new QPushButton (QIcon::fromTheme ("dialog-ok-apply", style ()->standardIcon (QStyle::SP_DialogApplyButton)), "Apply");
    apply->setObjectName ("plugin-apply");
    apply->setAutoDefault (false); form->addRow (apply);
    auto* tabs = new QTabWidget; log = new QPlainTextEdit; result = new QPlainTextEdit;
    log->setReadOnly (true); result->setReadOnly (true); log->setMaximumBlockCount (2000);
    result->setObjectName ("plugin-last-result"); log->setObjectName ("plugin-log");
    tabs->addTab (log, "Process log"); tabs->addTab (result, "Latest result"); tabs->setMinimumHeight (100); layout->addWidget (tabs, 1);
    message = new QLabel; message->setWordWrap (true); message->setTextFormat (Qt::PlainText); layout->addWidget (message);
    connect (startup, &QComboBox::currentIndexChanged, this, [this] (int mode) { delay->setEnabled (mode == 2); });
    connect (list, &QTreeWidget::currentItemChanged, this, [this] (QTreeWidgetItem* item) {
      if (!item) return;
      selected.clear ();
      const auto id = item->data (0, Qt::UserRole).toString ().toStdString ();
      for (const auto& plugin: this->manager->plugins ()) if (plugin.manifest.id == id) { current = plugin; selected = id; break; }
      load_policy (); refresh ();
    });
    connect (startStop, &QToolButton::clicked, this, [this] {
      if (current.running || current.state == "Scheduled") execute (this, [&] { this->manager->stop (selected); });
      else launch_plugin (this->manager, selected, false);
    });
    connect (restart, &QToolButton::clicked, this, [this] { launch_plugin (this->manager, selected, true); });
    connect (force, &QToolButton::clicked, this, [this] { execute (this, [&] { this->manager->stop (selected, true); }); });
    connect (remove, &QToolButton::clicked, this, [this] {
      if (QMessageBox::question (this, "Uninstall Plugin", "Uninstall " + qs (current.manifest.name) + "?\nPlugin data will be retained.",
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        execute (this, [&] { this->manager->uninstall (selected); });
    });
    connect (apply, &QPushButton::clicked, this, [this] { execute (this, [this] { save (); }); });
    connect (manager, &QTMPluginManager::changed, this, [this] { refresh (); });
    connect (manager, &QTMPluginManager::installPrepared, this, [this] {
      if (!awaitingInstall) return;
      awaitingInstall= false;
      execute (this, [this] { review_install (this->manager, this); });
    });
    connect (manager, &QTMPluginManager::managementFinished, this, [this] (const QString& error) {
      message->setText (error.isEmpty () ? "Done." : error); refresh ();
    });
    connect (manager, &QObject::destroyed, this, [this] { setEnabled (false); });
    delay->setEnabled (false); refresh ();
  }
};
void manage_plugins (QWidget* parent) {
  auto* dialog = new QDialog (parent);
  dialog->setAttribute (Qt::WA_DeleteOnClose); dialog->setWindowTitle ("ATHENA Plugins"); dialog->resize (720, 800);
  auto* layout = new QVBoxLayout (dialog);
  layout->addWidget (qtm_plugin_preferences (qtm_plugin_manager (), dialog));
  auto* buttons = new QDialogButtonBox (QDialogButtonBox::Close);
  QObject::connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget (buttons); dialog->show ();
}
} // namespace

QWidget* qtm_plugin_preferences (QTMPluginManager* manager, QWidget* parent) {
  if (manager) return new plugin_page (manager, parent);
  auto* page = new QWidget (parent); auto* layout = new QVBoxLayout (page);
  auto* error = new QLabel ("Plugin service is unavailable. See the ATHENA startup log."); error->setWordWrap (true);
  layout->addWidget (error); layout->addStretch (); return page;
}
QMenu* qtm_plugins_menu (QWidget* parent) {
  auto* menu = parent->findChild<QMenu*> ("athena-plugins-menu", Qt::FindDirectChildrenOnly);
  if (menu) return menu;
  menu = new QMenu ("Plugins", parent); menu->setObjectName ("athena-plugins-menu");
  menu->addAction ("Manage plugins...", menu, [menu] { manage_plugins (menu->parentWidget ()); });
  QObject::connect (menu, &QMenu::aboutToShow, menu, [menu] {
    qDeleteAll (menu->findChildren<QMenu*> (QString (), Qt::FindDirectChildrenOnly));
    menu->clear ();
    menu->addAction ("Manage plugins...", menu, [menu] { manage_plugins (menu->parentWidget ()); });
    auto* manager = qtm_plugin_manager ();
    if (!manager) return;
    menu->addSeparator ();
    for (const auto& plugin: manager->plugins ()) {
      auto* actions = menu->addMenu (menu_text (plugin.manifest.name));
      actions->setToolTip (qs (plugin.manifest.id));
      const auto id = plugin.manifest.id;
      QPointer<QTMPluginManager> weak (manager);
      auto action = [actions, weak] (const QString& title, const char* icon, bool enabled, std::function<void (QTMPluginManager*)> run) {
        auto* a = actions->addAction (QIcon::fromTheme (icon), title, actions, [actions, weak, run] {
          if (weak) execute (actions, [&] { run (weak); });
        });
        a->setEnabled (enabled);
      };
      const bool active = plugin.running || plugin.state == "Scheduled";
      action (active ? "Stop" : "Start", active ? "media-playback-stop" : "media-playback-start",
        !manager->busy () && plugin.state != "Invalid", [id, active] (auto* m) {
          if (active) m->stop (id); else launch_plugin (m, id, false);
        });
      action ("Restart", "view-refresh", !manager->busy () && plugin.state != "Invalid",
        [id] (auto* m) { launch_plugin (m, id, true); });
      action ("Force quit", "process-stop", plugin.running, [id] (auto* m) { m->stop (id, true); });
      actions->addSeparator ();
      for (const auto& command: plugin.manifest.commands)
        action (menu_text (command.title), "system-run", plugin.running && plugin.state != "Stopping",
          [id, name = command.id] (auto* m) { m->command (id, name); });
    }
  });
  return menu;
}

QMenu* qtm_install_plugins_menu (QWidget* parent) {
  auto* menu = qtm_plugins_menu (parent);
  QAction* help = nullptr;
  for (QAction* action: parent->actions ()) {
    QString text = action->text ();
    text.remove ('&');
    if (text.trimmed () == QStringLiteral ("Help")) {
      help = action;
      break;
    }
  }
  if (help) parent->insertAction (help, menu->menuAction ());
  else parent->addAction (menu->menuAction ());
  return menu;
}
