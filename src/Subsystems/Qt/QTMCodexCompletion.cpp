#include "QTMCodexCompletion.hpp"

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
#include <QProcess>
#include <QPushButton>
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
    : QDialog (parent), bridgePath (bridge), homePath (home) {
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
    form->addRow ("Model:", modelCombo);
    form->addRow ("", modelDescription);
    form->addRow ("Reasoning effort:", effortCombo);
    form->addRow ("", fastCheck);
    form->addRow ("", webSearchCheck);
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
           << (webSearchCheck->isChecked ()? string ("on"): string ("off"));
    return result;
  }

private:
  QString bridgePath;
  QString homePath;
  QComboBox* modelCombo;
  QLabel* modelDescription;
  QComboBox* effortCombo;
  QCheckBox* fastCheck;
  QCheckBox* webSearchCheck;
  QLabel* statusLabel;
  QDialogButtonBox* buttons;
  QPushButton* reloadButton;
  int observedGeneration;
  QString fastTierId;

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
    }
    modelCombo->setCurrentIndex (defaultIndex);
    statusLabel->clear ();
    refreshModelOptions ();
  }

  void refreshModelOptions () {
    QJsonObject model=
      modelCombo->currentData (Qt::UserRole + 1).toJsonObject ();
    QString previous= effortCombo->currentData ().toString ();
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
qtm_codex_initialize_models (const string& bridge, const string& home) {
  loadModels (to_qstring (bridge), to_qstring (home), false);
}

array<string>
qtm_codex_completion_options (const string& bridge, const string& home) {
  qtm_codex_initialize_models (bridge, home);
  QTMCodexCompletionDialog dialog (to_qstring (bridge), to_qstring (home),
                                   QApplication::activeWindow ());
  if (dialog.exec () != QDialog::Accepted) return array<string> ();
  return dialog.options ();
}
