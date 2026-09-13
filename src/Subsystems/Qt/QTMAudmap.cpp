/******************************************************************************
* MODULE     : QTMAudmap.cpp
* DESCRIPTION: GUI-thread AUDMAP authorization dialogs and service lifecycle
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMAudmap.hpp"
#include "../AUDMAP/audmap_server.hpp"
#include "../AUDMAP/identity.hpp"
#include "../../ATHENA/Interop/resources.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <deque>
#include <thread>

using namespace athena::interop;

static std::filesystem::path authorization_path () {
  auto home = qEnvironmentVariable ("ATHENA_HOME_PATH");
  if (home.isEmpty ()) home = QDir::home ().filePath (".ATHENA");
  return QDir (home).filePath ("system/audmap/clients.json").toStdString ();
}

static void request_field (QFormLayout* form, const char* label,
                           const QString& text, const char* name) {
  form->setLabelAlignment (Qt::AlignLeft | Qt::AlignTop);
  auto* value = new QLabel (text);
  value->setObjectName (name);
  value->setTextFormat (Qt::PlainText);
  value->setWordWrap (true);
  value->setAlignment (Qt::AlignLeft | Qt::AlignTop);
  value->setTextInteractionFlags (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  value->setSizePolicy (QSizePolicy::Ignored, QSizePolicy::Preferred);
  form->addRow (label, value);
}

static QPlainTextEdit* request_json (const QString& text, const char* name,
                                   int maximum_lines, QWidget* parent) {
  auto* editor = new QPlainTextEdit (parent);
  editor->setObjectName (name);
  editor->setReadOnly (true);
  editor->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
  editor->setPlainText (text);
  const auto lines = std::clamp<qsizetype> (text.count ('\n') + 1, 2, maximum_lines);
  editor->setFixedHeight (editor->fontMetrics ().lineSpacing () * lines + 16);
  return editor;
}

struct QTMAudmap::impl: QObject {
  struct client_display { std::string name, key; };
  std::map<std::string, client_display> clients;
  authorization_store permissions {authorization_path ()}; // GUI-thread owned
  std::map<std::string, std::vector<QPointer<QDialog>>> dialogs;
  std::unique_ptr<local_server> server;
  std::deque<std::pair<std::string, std::function<void ()>>> pending;
  QPointer<QDialog> active;
  bool shutting_down = false;
  void next () {
    if (shutting_down || active || pending.empty ()) return;
    auto show = std::move (pending.front ().second);
    pending.pop_front ();
    show ();
  }
  void queue_dialog (std::string id, std::function<void ()> show) {
    QMetaObject::invokeMethod (this, [this, id = std::move (id), show = std::move (show)] () mutable {
      pending.emplace_back (std::move (id), std::move (show));
      next ();
    }, Qt::QueuedConnection);
  }
  impl () {
    authorization_ui ui;
    ui.connect = [this] (std::string id, std::string key, std::string name,
                         std::function<void (std::optional<trust_mode>)> reply) {
      queue_dialog (id, [this, id, key, name, reply] {
        clients[id] = {name, key};
        QString policy_error;
        try {
          if (const auto rule = permissions.lookup (key)) {
            reply (rule->allow ? std::optional<trust_mode> (rule->trust) : std::nullopt);
            QTimer::singleShot (0, this, [this] { next (); });
            return;
          }
        }
        catch (const std::exception& e) { policy_error = QString::fromUtf8 (e.what ()); }
        auto* dialog = create (id, "ATHENA Interop Connection");
        auto* layout = static_cast<QVBoxLayout*> (dialog->layout ());
        auto* label = new QLabel (QString::fromStdString (name), dialog);
        label->setTextFormat (Qt::PlainText);
        label->setWordWrap (true);
        layout->addWidget (label);
        layout->addWidget (new QLabel ("Client public key:", dialog));
        auto* identity = new QLabel (QString::fromStdString (key), dialog);
        identity->setTextFormat (Qt::PlainText);
        identity->setWordWrap (true);
        identity->setTextInteractionFlags (Qt::TextSelectableByMouse);
        layout->addWidget (identity);
        auto* mode = new QComboBox (dialog);
        mode->addItem ("Full access", static_cast<int> (trust_mode::full_access));
        mode->addItem ("Confirm operations", static_cast<int> (trust_mode::confirm_operations));
        mode->addItem ("Confirm every request", static_cast<int> (trust_mode::confirm_requests));
        mode->setCurrentIndex (1);
        layout->addWidget (mode);
        auto* error = new QLabel (policy_error, dialog);
        error->setTextFormat (Qt::PlainText); error->setWordWrap (true);
        layout->addWidget (error);
        auto* choices = new QGridLayout ();
        layout->addLayout (choices);
        const char* labels[] = {"Allow (this time only)", "Allow (always)",
                                "Reject (this time only)", "Reject (always)"};
        for (int i = 0; i < 4; ++i) {
          auto* button = new QPushButton (labels[i], dialog);
          button->setAutoDefault (false);
          choices->addWidget (button, i / 2, i % 2);
          QObject::connect (button, &QPushButton::clicked, dialog, [this, dialog, mode, key, error, i] {
            if (i % 2) {
              try { permissions.remember (key, {i < 2, static_cast<trust_mode> (mode->currentData ().toInt ())}); }
              catch (const std::exception& e) { error->setText (QString::fromUtf8 (e.what ())); return; }
            }
            dialog->done (i < 2 ? QDialog::Accepted : QDialog::Rejected);
          });
        }
        QObject::connect (dialog, &QDialog::finished, dialog, [mode, reply] (int result) {
          if (result == QDialog::Accepted) reply (static_cast<trust_mode> (mode->currentData ().toInt ()));
          else reply (std::nullopt);
        });
        dialog->open ();
      });
    };
    ui.confirm = [this] (std::string id, value request, std::function<void (bool)> reply) {
      queue_dialog (id, [this, id, request = std::move (request), reply] {
        const auto client = clients.find (id);
        const auto& frame = request.is_object () && request.contains ("request") ? request["request"] : request;
        const bool operation = frame.is_array () && frame.size () == 6 &&
          frame[0] == static_cast<unsigned> (opcode::opr) && frame[4].is_string () &&
          request.contains ("resource_type") && request["resource_type"].is_string () &&
          request.contains ("resource_identity") && request["resource_identity"].is_string () &&
          request.contains ("selection") && request["selection"].is_string ();
        const bool resolution = frame.is_array () && frame.size () == 4 &&
          frame[0] == static_cast<unsigned> (opcode::req) && frame[2].is_string ();
        if (client == clients.end () || (!operation && !resolution)) {
          reply (false);
          QTimer::singleShot (0, this, [this] { next (); });
          return;
        }
        auto* dialog = create (id, "Confirm Interop Request");
        auto* layout = static_cast<QVBoxLayout*> (dialog->layout ());
        auto* scroll = new QScrollArea (dialog);
        scroll->setWidgetResizable (true);
        scroll->setFrameShape (QFrame::NoFrame);
        auto* content = new QWidget (scroll);
        auto* body = new QVBoxLayout (content);
        body->setContentsMargins (0, 0, 0, 0);
        auto* fields = new QFormLayout ();
        fields->setFieldGrowthPolicy (QFormLayout::AllNonFixedFieldsGrow);
        fields->setVerticalSpacing (12);
        body->addLayout (fields);
        request_field (fields, "Client", QString::fromStdString (client->second.name), "audmap_client");
        // Decode only the AUDMAP envelope. Resource types, identities, command
        // names and arguments remain opaque data, with no domain lookups.
        if (operation) {
          request_field (fields, "Command", QString::fromStdString (frame[4].get<std::string> ()), "audmap_command");
          request_field (fields, "Accessor", QString ("Handle %1 (ticket %2)")
            .arg (QString::fromStdString (frame[3].dump ()), QString::fromStdString (frame[1].dump ())), "audmap_accessor");
          request_field (fields, "Resource type", QString::fromStdString (request["resource_type"].get<std::string> ()), "audmap_resource_type");
          request_field (fields, "Resource ID", QString::fromStdString (request["resource_identity"].get<std::string> ()), "audmap_resource_id");
          request_field (fields, "Selection", QString::fromStdString (request["selection"].get<std::string> ()), "audmap_selection");
          body->addWidget (new QLabel ("Arguments", content));
          body->addWidget (request_json (QString::fromStdString (frame[5].dump (2)), "audmap_arguments", 8, content));
        }
        else {
          request_field (fields, "Request", "Resolve (REQ)", "audmap_request");
          request_field (fields, "Ticket", QString::fromStdString (frame[1].dump ()), "audmap_ticket");
          request_field (fields, "Selection", QString::fromStdString (frame[2].get<std::string> ()), "audmap_selection");
          const auto& projection = frame[3];
          QString projection_text = QString::fromStdString (projection.dump ());
          if (projection.is_array () && !projection.empty ()) {
            if (projection[0] == 0) projection_text = "Full tree";
            else if (projection[0] == 1) projection_text = projection.size () == 2 ?
              QString ("Leaves (limit %1)").arg (QString::fromStdString (projection[1].dump ())) : "Leaves";
          }
          request_field (fields, "Projection", projection_text, "audmap_projection");
        }
        auto* toggle = new QToolButton (content);
        toggle->setObjectName ("audmap_details_toggle");
        toggle->setText ("Connection identity and raw request");
        toggle->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType (Qt::RightArrow);
        toggle->setCheckable (true);
        body->addWidget (toggle, 0, Qt::AlignLeft);
        auto* details = new QWidget (content);
        details->setObjectName ("audmap_details");
        auto* detail_layout = new QVBoxLayout (details);
        detail_layout->setContentsMargins (0, 0, 0, 0);
        auto* identity_fields = new QFormLayout ();
        detail_layout->addLayout (identity_fields);
        request_field (identity_fields, "Client public key", QString::fromStdString (client->second.key), "audmap_public_key");
        request_field (identity_fields, "Connection ID", QString::fromStdString (id), "audmap_connection_id");
        if (operation) request_field (identity_fields, "Operation ID", QString::fromStdString (frame[2].dump ()), "audmap_operation_id");
        detail_layout->addWidget (request_json (QString::fromStdString (request.dump (2)), "audmap_raw_request", 10, details));
        body->addWidget (details);
        details->hide ();
        body->addStretch ();
        scroll->setWidget (content);
        layout->addWidget (scroll, 1);
        auto* buttons = new QDialogButtonBox (dialog);
        auto* reject = buttons->addButton ("Reject", QDialogButtonBox::RejectRole);
        auto* allow = buttons->addButton ("Allow this request", QDialogButtonBox::AcceptRole);
        reject->setObjectName ("audmap_reject");
        allow->setObjectName ("audmap_allow");
        reject->setDefault (true);
        allow->setAutoDefault (false);
        layout->addWidget (buttons);
        QObject::connect (buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        QObject::connect (buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        QObject::connect (dialog, &QDialog::finished, dialog, [reply] (int result) { reply (result == QDialog::Accepted); });
        const auto available = dialog->screen ()->availableGeometry ();
        dialog->setFixedWidth (std::min (640, available.width () - 40));
        const auto height_limit = available.height () - 40;
        const auto fit_contents = [dialog, layout, body, buttons, height_limit] {
          const auto margins = layout->contentsMargins ();
          int height = body->totalHeightForWidth (dialog->width () - margins.left () - margins.right ());
          if (height < 0) height = body->totalSizeHint ().height ();
          height += margins.top () + margins.bottom () + layout->spacing () + buttons->sizeHint ().height ();
          dialog->resize (dialog->width (), std::min (height, height_limit));
        };
        fit_contents ();
        QObject::connect (toggle, &QToolButton::toggled, dialog, [toggle, details, fit_contents] (bool expanded) {
          toggle->setArrowType (expanded ? Qt::DownArrow : Qt::RightArrow);
          details->setVisible (expanded);
          fit_contents ();
        });
        dialog->open ();
      });
    };
    ui.disconnect = [this] (std::string id) {
      QMetaObject::invokeMethod (this, [this, id] {
        clients.erase (id);
        pending.erase (std::remove_if (pending.begin (), pending.end (),
          [&] (const auto& request) { return request.first == id; }), pending.end ());
        auto it = dialogs.find (id);
        if (it == dialogs.end ()) return;
        auto closing = std::move (it->second); dialogs.erase (it);
        for (const auto& d: closing) if (d) d->reject ();
      }, Qt::QueuedConnection);
    };
    const auto count = std::max (1u, std::min (8u, std::thread::hardware_concurrency ()));
    server = std::make_unique<local_server> (native_resolvers (), std::move (ui), count);
    qInfo ("ATHENA AUDMAP endpoint: %s", server->discovery_file ().c_str ());
  }
  QDialog* create (const std::string& id, const char* title) {
    auto* dialog = new QDialog ();
    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setWindowTitle (title);
    dialog->setFixedWidth (560);
    new QVBoxLayout (dialog);
    dialogs[id].push_back (dialog);
    active = dialog;
    QObject::connect (dialog, &QDialog::finished, this, [this, id] {
      active = nullptr;
      dialogs.erase (id);
      QTimer::singleShot (0, this, [this] { next (); });
    });
    return dialog;
  }
  ~impl () override {
    shutting_down = true;
    server.reset ();
    pending.clear ();
    for (auto& entry: dialogs) for (const auto& dialog: entry.second) if (dialog) delete dialog;
  }
};

QTMAudmap::QTMAudmap (): implementation (std::make_unique<impl> ()) {}
QTMAudmap::~QTMAudmap () = default;

namespace {
std::unique_ptr<QTMAudmap> desktop_interop;
}

void qt_audmap_start () {
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  if (!desktop_interop) desktop_interop = std::make_unique<QTMAudmap> ();
}

void qt_audmap_stop () {
  if (!desktop_interop) return;
  Q_ASSERT (QThread::currentThread () == qApp->thread ());
  desktop_interop.reset ();
}
