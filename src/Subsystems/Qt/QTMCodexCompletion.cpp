#include "QTMCodexCompletion.hpp"

#include "actor_transport.hpp"
#include "boot.hpp"
#include "qt_utilities.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaObject>
#include <QProcess>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {

enum class CatalogState { Empty, Loading, Ready, Failed };

CatalogState catalogState= CatalogState::Empty;
QJsonArray catalogModels;
QString catalogError;
QString catalogBridge;
QString catalogHome;
int catalogGeneration= 0;

void
loadModels (const QString& bridge, const QString& home, bool force) {
  if (!force && catalogBridge == bridge && catalogHome == home &&
      (catalogState == CatalogState::Loading ||
       catalogState == CatalogState::Ready))
    return;

  catalogBridge= bridge;
  catalogHome= home;
  catalogModels= QJsonArray ();
  catalogError.clear ();
  catalogState= CatalogState::Loading;
  catalogGeneration++;
  const int requestGeneration= catalogGeneration;

  QProcess* process= new QProcess (QApplication::instance ());
  process->setProcessChannelMode (QProcess::SeparateChannels);
  QObject::connect (
    process, qOverload<int,QProcess::ExitStatus> (&QProcess::finished), process,
    [process, requestGeneration] (int exitCode,
                                  QProcess::ExitStatus exitStatus) {
      if (requestGeneration != catalogGeneration) {
        process->deleteLater ();
        return;
      }
      QByteArray output= process->readAllStandardOutput ();
      QByteArray diagnostics= process->readAllStandardError ();
      QJsonParseError parseError;
      QJsonDocument document= QJsonDocument::fromJson (output, &parseError);
      if (exitStatus == QProcess::NormalExit && exitCode == 0 &&
          parseError.error == QJsonParseError::NoError && document.isArray () &&
          !document.array ().isEmpty ()) {
        catalogModels= document.array ();
        catalogState= CatalogState::Ready;
      }
      else {
        catalogState= CatalogState::Failed;
        catalogError= QString::fromUtf8 (diagnostics).trimmed ();
        if (catalogError.isEmpty ())
          catalogError= parseError.error == QJsonParseError::NoError?
            "Codex returned no available models":
            QString ("Invalid model list from Codex: %1")
              .arg (parseError.errorString ());
      }
      catalogGeneration++;
      process->deleteLater ();
    });
  QObject::connect (
    process, &QProcess::errorOccurred, process,
    [process, requestGeneration] (QProcess::ProcessError error) {
      if (error != QProcess::FailedToStart) return;
      if (requestGeneration != catalogGeneration) {
        process->deleteLater ();
        return;
      }
      catalogState= CatalogState::Failed;
      catalogError= QString ("Could not start Codex bridge: %1")
                       .arg (process->errorString ());
      catalogGeneration++;
      process->deleteLater ();
    });
  process->start (bridge, {"--list-models", "--codex-home", home});
}

QString
modelFastTier (const QJsonObject& model) {
  for (const QJsonValue& value: model.value ("serviceTiers").toArray ()) {
    QJsonObject tier= value.toObject ();
    if (tier.value ("name").toString ().compare ("Fast",
                                                  Qt::CaseInsensitive) == 0)
      return tier.value ("id").toString ();
  }
  return QString ();
}

class QTMCodexCompletionDialog final: public QDialog {
public:
  QTMCodexCompletionDialog (const QString& bridge, const QString& home,
                            QWidget* parent)
    : QDialog (parent), bridgePath (bridge), homePath (home),
      rememberChoices (
        get_user_preference ("codex completion remember choices", "off") ==
        "on"),
      rememberedModel (to_qstring (
        get_user_preference ("codex completion model", ""))),
      rememberedEffort (to_qstring (
        get_user_preference ("codex completion effort", ""))),
      rememberedFast (
        get_user_preference ("codex completion fast", "off") == "on") {
    setWindowTitle ("AI completion (custom)");
    setMinimumWidth (520);

    QVBoxLayout* layout= new QVBoxLayout (this);
    QFormLayout* form= new QFormLayout;
    modelCombo= new QComboBox (this);
    modelDescription= new QLabel (this);
    modelDescription->setWordWrap (true);
    effortCombo= new QComboBox (this);
    fastCheck= new QCheckBox ("Use fast service tier", this);
    webSearchCheck= new QCheckBox ("Allow Codex web search", this);
    temporaryBufferCheck=
      new QCheckBox ("Open answer in a temporary buffer", this);
    rememberCheck= new QCheckBox ("Remember last choice", this);
    rememberCheck->setChecked (rememberChoices);
    if (rememberChoices) {
      webSearchCheck->setChecked (
        get_user_preference ("codex completion web search", "off") == "on");
      temporaryBufferCheck->setChecked (
        get_user_preference ("codex completion destination", "document") ==
        "temporary-buffer");
    }
    form->addRow ("Model:", modelCombo);
    form->addRow ("", modelDescription);
    form->addRow ("Reasoning effort:", effortCombo);
    form->addRow ("", fastCheck);
    form->addRow ("", webSearchCheck);
    form->addRow ("", temporaryBufferCheck);
    form->addRow ("", rememberCheck);
    layout->addLayout (form);

    statusLabel= new QLabel (this);
    statusLabel->setWordWrap (true);
    layout->addWidget (statusLabel);

    buttons= new QDialogButtonBox (QDialogButtonBox::Ok |
                                    QDialogButtonBox::Cancel, this);
    reloadButton= buttons->addButton ("Reload models",
                                      QDialogButtonBox::ResetRole);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (reloadButton, &QPushButton::clicked, this, [this] {
      loadModels (bridgePath, homePath, true);
      refreshCatalog (true);
    });
    connect (modelCombo, qOverload<int> (&QComboBox::currentIndexChanged),
             this, [this] (int) { refreshModelOptions (); });

    observedGeneration= -1;
    refreshCatalog (true);
    QTimer* timer= new QTimer (this);
    timer->setInterval (100);
    connect (timer, &QTimer::timeout, this, [this] {
      if (observedGeneration != catalogGeneration) refreshCatalog (false);
    });
    timer->start ();
  }

  array<string> options () const {
    array<string> result;
    result << from_qstring (modelCombo->currentData ().toString ())
           << from_qstring (effortCombo->currentData ().toString ())
           << from_qstring (fastCheck->isChecked ()? fastTierId: QString ())
           << (webSearchCheck->isChecked ()? string ("on"): string ("off"))
           << (temporaryBufferCheck->isChecked ()?
                 string ("temporary-buffer"): string ("document"))
           << (rememberCheck->isChecked ()? string ("on"): string ("off"));
    return result;
  }

  void saveChoices () const {
    const bool remember= rememberCheck->isChecked ();
    set_user_preference ("codex completion remember choices",
                         remember? "on": "off");
    if (remember) {
      set_user_preference (
        "codex completion model",
        from_qstring (modelCombo->currentData ().toString ()));
      set_user_preference (
        "codex completion effort",
        from_qstring (effortCombo->currentData ().toString ()));
      set_user_preference ("codex completion fast",
                           fastCheck->isChecked ()? "on": "off");
      set_user_preference ("codex completion web search",
                           webSearchCheck->isChecked ()? "on": "off");
      set_user_preference (
        "codex completion destination",
        temporaryBufferCheck->isChecked ()? "temporary-buffer": "document");
    }
    else {
      set_user_preference ("codex completion model", "");
      set_user_preference ("codex completion effort", "");
      set_user_preference ("codex completion fast", "off");
      set_user_preference ("codex completion web search", "off");
      set_user_preference ("codex completion destination", "document");
    }
    save_user_preferences ();
  }

private:
  QString bridgePath;
  QString homePath;
  QComboBox* modelCombo;
  QLabel* modelDescription;
  QComboBox* effortCombo;
  QCheckBox* fastCheck;
  QCheckBox* webSearchCheck;
  QCheckBox* temporaryBufferCheck;
  QCheckBox* rememberCheck;
  QLabel* statusLabel;
  QDialogButtonBox* buttons;
  QPushButton* reloadButton;
  int observedGeneration;
  QString fastTierId;
  bool rememberChoices;
  QString rememberedModel;
  QString rememberedEffort;
  bool rememberedFast;

  void refreshCatalog (bool force) {
    if (!force && observedGeneration == catalogGeneration) return;
    observedGeneration= catalogGeneration;
    modelCombo->clear ();
    const bool ready= catalogState == CatalogState::Ready;
    modelCombo->setEnabled (ready);
    effortCombo->setEnabled (ready);
    buttons->button (QDialogButtonBox::Ok)->setEnabled (ready);
    reloadButton->setVisible (catalogState == CatalogState::Failed);

    if (catalogState == CatalogState::Loading) {
      modelCombo->addItem ("Loading available models...");
      statusLabel->setText ("ATHENA is loading the model list from Codex.");
      refreshModelOptions ();
      return;
    }
    if (catalogState == CatalogState::Failed) {
      modelCombo->addItem ("Models unavailable");
      statusLabel->setText (catalogError);
      refreshModelOptions ();
      return;
    }
    if (!ready) {
      loadModels (bridgePath, homePath, false);
      refreshCatalog (true);
      return;
    }

    int defaultIndex= 0;
    int rememberedIndex= -1;
    for (const QJsonValue& value: catalogModels) {
      QJsonObject model= value.toObject ();
      QString display= model.value ("displayName").toString ();
      QString id= model.value ("model").toString ();
      if (display.isEmpty ()) display= id;
      modelCombo->addItem (display, id);
      modelCombo->setItemData (modelCombo->count () - 1, model,
                               Qt::UserRole + 1);
      if (model.value ("isDefault").toBool ())
        defaultIndex= modelCombo->count () - 1;
      if (rememberChoices && id == rememberedModel)
        rememberedIndex= modelCombo->count () - 1;
    }
    modelCombo->setCurrentIndex (
      rememberedIndex >= 0? rememberedIndex: defaultIndex);
    statusLabel->clear ();
    refreshModelOptions ();
    if (rememberChoices && fastCheck->isEnabled ())
      fastCheck->setChecked (rememberedFast);
  }

  void refreshModelOptions () {
    QJsonObject model=
      modelCombo->currentData (Qt::UserRole + 1).toJsonObject ();
    QString previous= effortCombo->currentData ().toString ();
    if (previous.isEmpty () && rememberChoices)
      previous= rememberedEffort;
    effortCombo->clear ();
    QString defaultEffort= model.value ("defaultReasoningEffort").toString ();
    int defaultIndex= 0;
    int previousIndex= -1;
    for (const QJsonValue& value:
         model.value ("supportedReasoningEfforts").toArray ()) {
      QJsonObject option= value.toObject ();
      QString effort= option.value ("reasoningEffort").toString ();
      effortCombo->addItem (effort, effort);
      effortCombo->setItemData (effortCombo->count () - 1,
                                option.value ("description").toString (),
                                Qt::ToolTipRole);
      if (effort == defaultEffort) defaultIndex= effortCombo->count () - 1;
      if (effort == previous) previousIndex= effortCombo->count () - 1;
    }
    effortCombo->setCurrentIndex (previousIndex >= 0?
                                   previousIndex: defaultIndex);
    modelDescription->setText (model.value ("description").toString ());
    fastTierId= modelFastTier (model);
    const bool supportsFast= !fastTierId.isEmpty ();
    fastCheck->setEnabled (supportsFast);
    if (!supportsFast) fastCheck->setChecked (false);
    fastCheck->setToolTip (supportsFast? QString ():
      "The selected model does not advertise a fast service tier.");
  }
};

} // namespace

void
qtm_codex_initialize_models (string bridge, string home) {
  QCoreApplication* app= QCoreApplication::instance ();
  if (app != nullptr && QThread::currentThread () != app->thread ()) {
    athena_blob_id bridgeId=
      actor_text_registry::instance ().store (std::move (bridge));
    athena_blob_id homeId=
      actor_text_registry::instance ().store (std::move (home));
    bool queued= QMetaObject::invokeMethod (
      app,
      [bridgeId, homeId] () {
        string queuedBridge=
          actor_text_registry::instance ().take (bridgeId);
        string queuedHome= actor_text_registry::instance ().take (homeId);
        qtm_codex_initialize_models (std::move (queuedBridge),
                                     std::move (queuedHome));
      },
      Qt::QueuedConnection);
    if (!queued) {
      (void) actor_text_registry::instance ().discard (bridgeId);
      (void) actor_text_registry::instance ().discard (homeId);
    }
    return;
  }
  loadModels (to_qstring (bridge), to_qstring (home), false);
}

array<string>
qtm_codex_completion_options (const string& bridge, const string& home) {
  qtm_codex_initialize_models (bridge, home);
  QTMCodexCompletionDialog dialog (to_qstring (bridge), to_qstring (home),
                                   QApplication::activeWindow ());
  if (dialog.exec () != QDialog::Accepted) return array<string> ();
  dialog.saveChoices ();
  return dialog.options ();
}
