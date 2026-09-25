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
  QComboBox *startup, *access, *trust;
  QSpinBox* delay;
  QTableWidget* rules;
  QToolButton *remove, *startStop, *restart, *force, *install;
  QPushButton* apply;
  QProgressBar* progress;
  QLabel *identity, *status, *message;
  QPlainTextEdit *log, *result;
  std::string selected;
  QTMPluginInfo current;
  void load_policy () {
    identity->setText (qs (current.manifest.id));
    startup->setCurrentIndex (static_cast<int> (current.policy.startup));
    access->setCurrentIndex (static_cast<int> (current.policy.access));
    trust->setCurrentIndex (static_cast<int> (current.policy.trust));
    delay->setValue (current.policy.delaySeconds);
    rules->setRowCount (0);
    for (const auto& [type, commands]: current.policy.commands) {
      const int row = rules->rowCount (); rules->insertRow (row);
      QStringList names; for (const auto& command: commands) names << qs (command);
      rules->setItem (row, 0, new QTableWidgetItem (qs (type)));
      rules->setItem (row, 1, new QTableWidgetItem (names.join (", ")));
    }
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
    if (QMessageBox::warning (this, "Install ATHENA Plugin",
      "Plugins run native code with your user account. AUDMAP permissions are not an operating-system sandbox.\n\n"
      "Install this package? It will remain stopped until started or configured for startup.",
      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok) return;
    execute (this, [&] { manager->install (path.toStdString ()); });
  }
  void save () {
    QTMPluginPolicy policy;
    policy.startup = static_cast<QTMPluginPolicy::Startup> (startup->currentIndex ());
    policy.access = static_cast<QTMPluginPolicy::Access> (access->currentIndex ());
    policy.trust = static_cast<athena::interop::trust_mode> (trust->currentIndex ());
    policy.delaySeconds = delay->value ();
    for (int row = 0; row < rules->rowCount (); ++row) {
      const auto* type = rules->item (row, 0); const auto* commands = rules->item (row, 1);
      if (!type || type->text ().trimmed ().isEmpty ()) throw std::invalid_argument ("Each permission rule needs a resource type");
      const auto key = type->text ().trimmed ().toStdString ();
      if (policy.commands.count (key)) throw std::invalid_argument ("Duplicate resource type in permission rules");
      auto& names = policy.commands[key];
      if (commands) for (const auto& name: commands->text ().split (',', Qt::SkipEmptyParts))
        if (!name.trimmed ().isEmpty ()) names.insert (name.trimmed ().toStdString ());
    }
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
    access = new QComboBox; access->setObjectName ("plugin-access"); access->addItems ({"Read only", "Full API access", "Custom commands"});
    form->addRow ("API permissions", access);
    trust = new QComboBox; trust->setObjectName ("plugin-trust");
    trust->addItems ({"No confirmation", "Confirm operations", "Confirm every request"}); form->addRow ("Confirmation", trust);
    auto* custom = new QWidget; auto* customLayout = new QVBoxLayout (custom); customLayout->setContentsMargins (0, 0, 0, 0);
    rules = new QTableWidget (0, 2); rules->setObjectName ("plugin-permission-rules");
    rules->setHorizontalHeaderLabels ({"Resource type (* = fallback)", "Allowed commands (comma-separated)"});
    rules->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch); rules->setMaximumHeight (150);
    customLayout->addWidget (rules); auto* ruleTools = new QHBoxLayout;
    auto* addRule = tool (custom, ruleTools, "list-add", "Add permission rule");
    auto* removeRule = tool (custom, ruleTools, "list-remove", "Remove selected permission rule");
    if (addRule->icon ().isNull ()) addRule->setText ("+");
    if (removeRule->icon ().isNull ()) removeRule->setText ("-");
    ruleTools->addStretch (); customLayout->addLayout (ruleTools); form->addRow (custom);
    apply = new QPushButton (QIcon::fromTheme ("dialog-ok-apply", style ()->standardIcon (QStyle::SP_DialogApplyButton)), "Apply");
    apply->setObjectName ("plugin-apply");
    apply->setAutoDefault (false); form->addRow (apply);
    auto* tabs = new QTabWidget; log = new QPlainTextEdit; result = new QPlainTextEdit;
    log->setReadOnly (true); result->setReadOnly (true); log->setMaximumBlockCount (2000);
    result->setObjectName ("plugin-last-result"); log->setObjectName ("plugin-log");
    tabs->addTab (log, "Process log"); tabs->addTab (result, "Latest result"); tabs->setMinimumHeight (100); layout->addWidget (tabs, 1);
    message = new QLabel; message->setWordWrap (true); message->setTextFormat (Qt::PlainText); layout->addWidget (message);
    connect (startup, &QComboBox::currentIndexChanged, this, [this] (int mode) { delay->setEnabled (mode == 2); });
    connect (access, &QComboBox::currentIndexChanged, this, [custom] (int mode) { custom->setVisible (mode == 2); });
    connect (addRule, &QToolButton::clicked, this, [this] { rules->insertRow (rules->rowCount ()); });
    connect (removeRule, &QToolButton::clicked, this, [this] { if (rules->currentRow () >= 0) rules->removeRow (rules->currentRow ()); });
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
    connect (manager, &QTMPluginManager::managementFinished, this, [this] (const QString& error) {
      message->setText (error.isEmpty () ? "Done." : error); refresh ();
    });
    connect (manager, &QObject::destroyed, this, [this] { setEnabled (false); });
    custom->hide (); delay->setEnabled (false); refresh ();
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
