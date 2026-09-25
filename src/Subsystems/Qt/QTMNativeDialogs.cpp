/******************************************************************************
* MODULE     : QTMNativeDialogs.cpp
* DESCRIPTION: Native Qt replacements for legacy Scheme dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include "QTMNativeDialogs.hpp"

#include "QTMMenuHelper.hpp"
#include "QTMPrintDialog.hpp"
#include "QTMPrinterSettings.hpp"
#include "tm_window.hpp"
#include "qt_sys_utils.hpp"
#include "qt_utilities.hpp"
#include "scheme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QCompleter>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QKeySequenceEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>
#include <functional>
#include <memory>
#include <QSet>

namespace {

int tooltip_window_handle= 0;

struct TooltipRequest {
  tree doc;
  tree style;
  int x= 0;
  int y= 0;
};

QTMPrinterSettings*&
printer_settings () {
  static QTMPrinterSettings* settings= nullptr;
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
  if (settings == nullptr) settings= new CupsQTMPrinterSettings ();
#endif
  return settings;
}

QString
qs (string s) {
  return utf8_to_qstring (s);
}

string
tm_string (const QString& s) {
  return from_qstring_utf8 (s);
}

QString
expanded_path (QString path) {
  const QString marker= "$ATHENA_PATH";
  if (path.startsWith (marker))
    path.replace (0, marker.size (), qEnvironmentVariable ("ATHENA_PATH"));
  const QString patternMarker= "$ATHENA_PATTERN_PATH";
  if (path.startsWith (patternMarker)) {
    QString base= qEnvironmentVariable ("ATHENA_PATTERN_PATH");
    if (base.isEmpty ())
      base= QDir (qEnvironmentVariable ("ATHENA_PATH")).filePath ("misc/patterns");
    path.replace (0, patternMarker.size (), base);
  }
  return QDir::cleanPath (path);
}

void
invoke_gui_blocking (const std::function<void ()>& fn) {
  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;
  if (QThread::currentThread () == app->thread ()) {
    fn ();
    return;
  }
  QMetaObject::invokeMethod (app, fn, Qt::BlockingQueuedConnection);
}

struct QtInteractiveField {
  QString prompt;
  QString type;
  QStringList proposals;
};

QWidget*
interactive_field_widget (const QtInteractiveField& field,
                          QWidget* parent) {
  if (field.type == QStringLiteral ("password")) {
    QLineEdit* edit= new QLineEdit (parent);
    edit->setEchoMode (QLineEdit::Password);
    return edit;
  }

  if (field.type.endsWith (QStringLiteral ("file")) ||
      field.type == QStringLiteral ("directory")) {
    QLineEdit* edit= new QLineEdit (parent);
    if (!field.proposals.isEmpty ()) edit->setText (field.proposals[0]);
    QCompleter* completer= new QCompleter (edit);
    QFileSystemModel* model= new QFileSystemModel (edit);
    model->setRootPath (QDir::homePath ());
    completer->setModel (model);
    edit->setCompleter (completer);
    return edit;
  }

  QTMComboBox* combo= new QTMComboBox (parent);
  combo->setEditable (true);
  combo->addItems (field.proposals);
  QTMLineEdit* line= new QTMLineEdit (
    combo, tm_string (field.type), "1w", WIDGET_STYLE_MINI, command ());
  combo->setLineEdit (line);
  combo->setSizeAdjustPolicy (QComboBox::AdjustToContents);
  combo->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
  combo->setMinimumWidth (160);
  combo->setDuplicatesEnabled (true);
  if (!field.proposals.isEmpty ()) combo->setEditText (field.proposals[0]);
  return combo;
}

string
interactive_field_value (QWidget* widget) {
  if (QComboBox* combo= qobject_cast<QComboBox*> (widget))
    return tm_string (combo->currentText ());
  if (QLineEdit* edit= qobject_cast<QLineEdit*> (widget))
    return tm_string (edit->text ());
  return "";
}

int
question_dialog (const QtInteractiveField& field) {
  QMessageBox box (QApplication::activeWindow ());
  box.setWindowTitle (QStringLiteral ("Question"));
  box.setIcon (QMessageBox::Question);
  box.setText (field.prompt);
  box.setTextFormat (Qt::PlainText);
  box.setStandardButtons (QMessageBox::NoButton);
  QVector<QPushButton*> buttons;
  for (const QString& proposal: field.proposals) {
    QString label= proposal;
    if (proposal == QStringLiteral ("yes")) label= QStringLiteral ("Yes");
    else if (proposal == QStringLiteral ("no")) label= QStringLiteral ("No");
    else if (proposal == QStringLiteral ("cancel")) label= QStringLiteral ("Cancel");
    buttons << box.addButton (label, QMessageBox::ActionRole);
  }
  if (!buttons.isEmpty ()) box.setDefaultButton (buttons[0]);
  box.exec ();
  for (int i=0; i<buttons.size (); ++i)
    if (box.clickedButton () == buttons[i]) return i;
  return -1;
}

class LinkedFileChoiceDialog: public QDialog {
public:
  LinkedFileChoiceDialog (const QString& name, const QStringList& items,
                          QWidget* parent): QDialog (parent) {
    setWindowTitle ("Open linked file");
    setMinimumWidth (620);
    QVBoxLayout* layout= new QVBoxLayout (this);
    QLabel* message= new QLabel (
      QString ("How should ATHENA open %1?").arg (name), this);
    message->setWordWrap (true);
    layout->addWidget (message);
    layout->addSpacing (8);

    for (int i=0; i+1<items.size (); i+=2) {
      QPushButton* button= new QPushButton (
        items[i], this);
      connect (button, &QPushButton::clicked, this, [this, value=items[i+1]] () {
        result= value;
        accept ();
      });
      layout->addWidget (button);
    }

    layout->addStretch (1);
    QDialogButtonBox* buttons= new QDialogButtonBox (QDialogButtonBox::Cancel,
                                                      this);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget (buttons);
  }

  QString result;
};

struct LinkedFileRequest {
  QString name;
  QStringList items;
  QString result;
};

class UnsavedBuffersDialog: public QDialog {
public:
  UnsavedBuffersDialog (const QStringList& buffers, bool restart,
                        QWidget* parent): QDialog (parent), restartMode (restart) {
    setWindowTitle ("Unsaved buffers");
    resize (720, 420);
    setMinimumSize (560, 300);
    QVBoxLayout* layout= new QVBoxLayout (this);
    QLabel* message= new QLabel (
      "The following buffers have unsaved changes. Checked buffers will be saved.",
      this);
    message->setWordWrap (true);
    layout->addWidget (message);

    QScrollArea* scroll= new QScrollArea (this);
    scroll->setWidgetResizable (true);
    QWidget* body= new QWidget (scroll);
    QVBoxLayout* bodyLayout= new QVBoxLayout (body);
    for (const QString& raw: buffers) {
      QString label= raw;
      const int slash= qMax (raw.lastIndexOf ('/'), raw.lastIndexOf ('\\'));
      if (slash >= 0 && slash + 1 < raw.size ()) label= raw.mid (slash + 1);
      QCheckBox* check= new QCheckBox (label, body);
      check->setToolTip (raw);
      check->setChecked (true);
      check->setProperty ("athenaBufferUrl", raw);
      checks << check;
      bodyLayout->addWidget (check);
    }
    bodyLayout->addStretch (1);
    scroll->setWidget (body);
    layout->addWidget (scroll, 1);

    QDialogButtonBox* buttons= new QDialogButtonBox (this);
    QPushButton* save= buttons->addButton (
      restart ? "Save and Restart" : "Save and Exit",
      QDialogButtonBox::AcceptRole);
    QPushButton* discard= buttons->addButton (
      restart ? "Restart Without Saving" : "Exit Without Saving",
      QDialogButtonBox::DestructiveRole);
    buttons->addButton (QDialogButtonBox::Cancel);
    connect (save, &QPushButton::clicked, this, [this] () {
      action= "save";
      accept ();
    });
    connect (discard, &QPushButton::clicked, this, [this] () {
      action= "discard";
      accept ();
    });
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget (buttons);
  }

  QStringList selectedBuffers () const {
    QStringList out;
    for (QCheckBox* check: checks)
      if (check->isChecked ()) out << check->property ("athenaBufferUrl").toString ();
    return out;
  }

  bool restartMode;
  QString action;
  QList<QCheckBox*> checks;
};

struct UnsavedRequest {
  QStringList buffers;
  bool restart= false;
  QString action;
  QStringList selected;
};

class TextReportDialog: public QDialog {
public:
  TextReportDialog (const QString& title, const QString& text, QWidget* parent)
    : QDialog (parent) {
    setWindowTitle (title);
    resize (780, 520);
    setMinimumSize (520, 320);
    QVBoxLayout* layout= new QVBoxLayout (this);
    QPlainTextEdit* report= new QPlainTextEdit (this);
    report->setReadOnly (true);
    report->setPlainText (text);
    layout->addWidget (report, 1);
    QDialogButtonBox* buttons= new QDialogButtonBox (QDialogButtonBox::Ok, this);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget (buttons);
  }
};

class LatexFormulaDialog: public QDialog {
public:
  explicit LatexFormulaDialog (QWidget* parent): QDialog (parent) {
    setWindowTitle ("Insert LaTeX formula");
    resize (620, 300);
    QVBoxLayout* layout= new QVBoxLayout (this);
    QFormLayout* form= new QFormLayout ();
    mode= new QComboBox (this);
    mode->addItems ({"inline", "display", "none"});
    formula= new QPlainTextEdit (this);
    formula->setPlaceholderText ("Enter LaTeX formula or fragment");
    formula->setMinimumHeight (150);
    form->addRow ("Mode:", mode);
    form->addRow ("LaTeX formula:", formula);
    layout->addLayout (form);
    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button (QDialogButtonBox::Ok)->setText ("Insert");
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget (buttons);
    formula->setFocus ();
  }

  QComboBox* mode;
  QPlainTextEdit* formula;
};

class BackgroundPreview: public QWidget {
public:
  explicit BackgroundPreview (QWidget* parent): QWidget (parent) {
    setMinimumHeight (220);
    setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  QString mode;
  QString source;
  QString foreground= "black";
  QString background= "white";
  int angle= 0;

protected:
  void paintEvent (QPaintEvent*) override {
    QPainter painter (this);
    painter.fillRect (rect (), palette ().brush (QPalette::Base));
    QRect area= rect ().adjusted (8, 8, -8, -8);
    painter.setPen (palette ().color (QPalette::Mid));
    painter.drawRect (area.adjusted (0, 0, -1, -1));
    area.adjust (1, 1, -1, -1);
    if (mode == "gradient") {
      const qreal radians= qDegreesToRadians ((qreal) angle);
      QPointF centre= area.center ();
      QPointF d (std::cos (radians), -std::sin (radians));
      const qreal radius= 0.5 * std::hypot (area.width (), area.height ());
      QLinearGradient gradient (centre - d * radius, centre + d * radius);
      QColor a (foreground), b (background);
      if (!a.isValid ()) a= Qt::black;
      if (!b.isValid ()) b= Qt::white;
      gradient.setColorAt (0, a);
      gradient.setColorAt (1, b);
      painter.fillRect (area, gradient);
      return;
    }
    QPixmap pixmap (expanded_path (source));
    if (!pixmap.isNull ()) {
      if (mode == "pattern") painter.drawTiledPixmap (area, pixmap);
      else {
        QPixmap scaled= pixmap.scaled (area.size (), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        QPoint topLeft= area.center () - QPoint (scaled.width () / 2,
                                                 scaled.height () / 2);
        painter.drawPixmap (topLeft, scaled);
      }
    }
    else {
      painter.setPen (palette ().color (QPalette::Text));
      painter.drawText (area, Qt::AlignCenter | Qt::TextWordWrap,
                        source.isEmpty () ? "No image selected" : source);
    }
  }
};

class BackgroundSelectorDialog: public QDialog {
public:
  BackgroundSelectorDialog (const QString& requestedMode,
                            const QStringList& initial, QWidget* parent)
    : QDialog (parent), selectorMode (requestedMode) {
    setWindowTitle (requestedMode == "gradient" ? "Gradient selector" :
                    requestedMode == "picture" ? "Background picture selector" :
                    "Pattern selector");
    resize (760, 560);
    setMinimumSize (620, 480);
    QVBoxLayout* layout= new QVBoxLayout (this);
    preview= new BackgroundPreview (this);
    preview->mode= requestedMode;
    layout->addWidget (preview, 1);

    QFormLayout* form= new QFormLayout ();
    sourceEdit= new QLineEdit (this);
    widthEdit= new QComboBox (this);
    heightEdit= new QComboBox (this);
    widthEdit->setEditable (true);
    heightEdit->setEditable (true);
    widthEdit->addItems ({"100%", "100@", "1cm", ""});
    heightEdit->addItems ({"100%", "100@", "1cm", ""});

    if (requestedMode != "gradient") {
      QWidget* sourceRow= new QWidget (this);
      QHBoxLayout* sourceLayout= new QHBoxLayout (sourceRow);
      sourceLayout->setContentsMargins (0, 0, 0, 0);
      QPushButton* browse= new QPushButton ("Browse…", sourceRow);
      sourceLayout->addWidget (sourceEdit, 1);
      sourceLayout->addWidget (browse);
      form->addRow ("Image:", sourceRow);
      connect (browse, &QPushButton::clicked, this, [this] () {
        QString start= expanded_path (sourceEdit->text ());
        QString file= QFileDialog::getOpenFileName (
          this, selectorMode == "picture" ? "Background picture" :
                                             "Background pattern",
          start, "Images (*.png *.jpg *.jpeg *.webp *.svg *.bmp);;All files (*)");
        if (!file.isEmpty ()) sourceEdit->setText (file);
      });
    }

    form->addRow ("Width:", widthEdit);
    form->addRow ("Height:", heightEdit);

    if (requestedMode == "gradient") {
      angleSpin= new QSpinBox (this);
      angleSpin->setRange (-180, 180);
      angleSpin->setSuffix ("°");
      foregroundEdit= colorEditor ("Foreground", form);
      backgroundEdit= colorEditor ("Background", form);
      form->insertRow (2, "Angle:", angleSpin);
    }
    else {
      recolorEdit= colorEditor ("Recolor", form, true);
      skinEdit= colorEditor ("Skin", form, true);
    }
    layout->addLayout (form);

    const QString source= initial.value (0,
      requestedMode == "gradient" ? QString ("athena-gradient-vertical") :
      QString ("$ATHENA_PATH/misc/patterns/neutral-pattern.png"));
    sourceEdit->setText (source);
    widthEdit->setCurrentText (initial.value (1,
      requestedMode == "pattern" ? QString ("1cm") : QString ("100%")));
    heightEdit->setCurrentText (initial.value (2,
      requestedMode == "pattern" ? QString ("100@") : QString ("100%")));
    if (requestedMode == "gradient") {
      angleSpin->setValue (initial.value (3, "0").toInt ());
      foregroundEdit->setText (initial.value (4, "black"));
      backgroundEdit->setText (initial.value (5, "white"));
    }
    else {
      recolorEdit->setText (initial.value (3));
      skinEdit->setText (initial.value (4));
    }

    auto update= [this] () { updatePreview (); };
    connect (sourceEdit, &QLineEdit::textChanged, this, update);
    connect (widthEdit, &QComboBox::currentTextChanged, this, update);
    connect (heightEdit, &QComboBox::currentTextChanged, this, update);
    if (angleSpin)
      connect (angleSpin, QOverload<int>::of (&QSpinBox::valueChanged),
               this, update);
    if (foregroundEdit)
      connect (foregroundEdit, &QLineEdit::textChanged, this, update);
    if (backgroundEdit)
      connect (backgroundEdit, &QLineEdit::textChanged, this, update);
    updatePreview ();

    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget (buttons);
  }

  QLineEdit* colorEditor (const QString& label, QFormLayout* form,
                          bool allowEmpty = false) {
    QWidget* row= new QWidget (this);
    QHBoxLayout* rowLayout= new QHBoxLayout (row);
    rowLayout->setContentsMargins (0, 0, 0, 0);
    QLineEdit* edit= new QLineEdit (row);
    if (allowEmpty) edit->setPlaceholderText ("None");
    QPushButton* choose= new QPushButton ("Choose…", row);
    rowLayout->addWidget (edit, 1);
    rowLayout->addWidget (choose);
    form->addRow (label + ":", row);
    connect (choose, &QPushButton::clicked, this, [this, edit] () {
      QColor initial (edit->text ());
      QColor color= QColorDialog::getColor (
        initial.isValid () ? initial : Qt::black, this, "Choose color",
        QColorDialog::ShowAlphaChannel);
      if (color.isValid ()) edit->setText (color.name (QColor::HexArgb));
    });
    return edit;
  }

  void updatePreview () {
    preview->source= sourceEdit->text ();
    if (angleSpin) preview->angle= angleSpin->value ();
    if (foregroundEdit) preview->foreground= foregroundEdit->text ();
    if (backgroundEdit) preview->background= backgroundEdit->text ();
    preview->update ();
  }

  QStringList result () const {
    QStringList values;
    values << sourceEdit->text ().trimmed ()
           << widthEdit->currentText ().trimmed ()
           << heightEdit->currentText ().trimmed ();
    if (selectorMode == "gradient")
      values << QString::number (angleSpin->value ())
             << foregroundEdit->text ().trimmed ()
             << backgroundEdit->text ().trimmed ();
    else
      values << recolorEdit->text ().trimmed ()
             << skinEdit->text ().trimmed ();
    return values;
  }

  QString selectorMode;
  BackgroundPreview* preview= nullptr;
  QLineEdit* sourceEdit= nullptr;
  QComboBox* widthEdit= nullptr;
  QComboBox* heightEdit= nullptr;
  QSpinBox* angleSpin= nullptr;
  QLineEdit* foregroundEdit= nullptr;
  QLineEdit* backgroundEdit= nullptr;
  QLineEdit* recolorEdit= nullptr;
  QLineEdit* skinEdit= nullptr;
};

struct BackgroundRequest {
  QString mode;
  QStringList initial;
  QStringList result;
};

QString
athena_shortcut_from_qt (const QKeySequence& sequence) {
  QStringList result;
  const QStringList parts=
    sequence.toString (QKeySequence::PortableText).split (',', Qt::SkipEmptyParts);
  for (QString part: parts) {
    part= part.trimmed ();
    bool shifted= part.contains ("Shift+");
    part.replace ("Shift+", "");
#ifdef Q_OS_MAC
    part.replace ("Meta+", "C-");
    part.replace ("Ctrl+", "M-");
#else
    part.replace ("Ctrl+", "C-");
    part.replace ("Meta+", "M-");
#endif
    part.replace ("Alt+", "A-");

    int split= part.lastIndexOf ('-');
    QString prefix= split >= 0 ? part.left (split + 1) : QString ();
    QString key= split >= 0 ? part.mid (split + 1) : part;
    if (key == "PgUp") key= "pageup";
    else if (key == "PgDown") key= "pagedown";
    else if (key == "Esc") key= "escape";
    else if (key == "Del") key= "delete";
    else if (key == "Ins") key= "insert";
    else if (key == "Space") key= "space";
    else if (key == "Backspace") key= "backspace";
    else if (key == "Return") key= "return";
    else if (key == "Enter") key= "enter";
    else if (key == "Tab") key= "tab";
    else if (key == "Home") key= "home";
    else if (key == "End") key= "end";
    else if (key == "Left") key= "left";
    else if (key == "Right") key= "right";
    else if (key == "Up") key= "up";
    else if (key == "Down") key= "down";
    else if (key == "^") key= "hat";

    if (key.size () == 1 && key[0].isLetter ())
      key= shifted ? key.toUpper () : key.toLower ();
    else if (shifted)
      prefix += "S-";
    result << prefix + key;
  }
  return result.join (' ');
}

QString
native_shortcut_label (const QString& raw) {
  QKeySequence sequence= to_qkeysequence (tm_string (raw));
  QString label= sequence.toString (QKeySequence::NativeText);
  return label.isEmpty () ? raw : label;
}

class ShortcutEditorDialog: public QDialog {
public:
  ShortcutEditorDialog (const QString& initialShortcut,
                        const QString& initialCommand,
                        const QStringList& initialEntries,
                        QWidget* parent)
    : QDialog (parent) {
    for (int i=0; i+1<initialEntries.size (); i+=2)
      entries.insert (initialEntries[i], initialEntries[i+1]);
    setWindowTitle ("Keyboard shortcuts");
    resize (720, 460);
    setMinimumSize (580, 360);
    QHBoxLayout* root= new QHBoxLayout (this);
    list= new QListWidget (this);
    list->setMinimumWidth (220);
    root->addWidget (list, 1);

    QWidget* editor= new QWidget (this);
    QVBoxLayout* editorLayout= new QVBoxLayout (editor);
    QFormLayout* form= new QFormLayout ();
    shortcut= new QLineEdit (editor);
    command= new QLineEdit (editor);
    shortcut->setPlaceholderText ("e.g. C-x C-s");
    QWidget* shortcutRow= new QWidget (editor);
    QHBoxLayout* shortcutLayout= new QHBoxLayout (shortcutRow);
    shortcutLayout->setContentsMargins (0, 0, 0, 0);
    QPushButton* record= new QPushButton ("Record…", shortcutRow);
    shortcutLayout->addWidget (shortcut, 1);
    shortcutLayout->addWidget (record);
    form->addRow ("Shortcut:", shortcutRow);
    form->addRow ("Command:", command);
    editorLayout->addLayout (form);
    QLabel* note= new QLabel (
      "Use ATHENA shortcut syntax (for example C-x, A-F1, or a multi-key sequence).",
      editor);
    note->setWordWrap (true);
    editorLayout->addWidget (note);
    editorLayout->addStretch (1);

    QHBoxLayout* actions= new QHBoxLayout ();
    QPushButton* remove= new QPushButton ("Remove", editor);
    QPushButton* clear= new QPushButton ("Clear", editor);
    QPushButton* apply= new QPushButton ("Update", editor);
    actions->addWidget (remove);
    actions->addWidget (clear);
    actions->addStretch (1);
    actions->addWidget (apply);
    editorLayout->addLayout (actions);
    root->addWidget (editor, 2);

    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Save | QDialogButtonBox::Cancel,
      Qt::Horizontal, editor);
    buttons->button (QDialogButtonBox::Save)->setText ("Save and Close");
    editorLayout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    connect (list, &QListWidget::currentItemChanged,
             this, [this] (QListWidgetItem* current, QListWidgetItem*) {
      if (current == nullptr) return;
      selectedShortcut= current->data (Qt::UserRole).toString ();
      shortcut->setText (selectedShortcut);
      command->setText (entries.value (selectedShortcut));
    });
    connect (clear, &QPushButton::clicked, this, [this] () {
      list->clearSelection ();
      selectedShortcut.clear ();
      shortcut->clear ();
      command->clear ();
      shortcut->setFocus ();
    });
    connect (record, &QPushButton::clicked, this, [this] () {
      QDialog dialog (this);
      dialog.setWindowTitle ("Record keyboard shortcut");
      QVBoxLayout* layout= new QVBoxLayout (&dialog);
      QLabel* prompt= new QLabel (
        "Press the key sequence to assign. Up to four strokes can be recorded.",
        &dialog);
      prompt->setWordWrap (true);
      layout->addWidget (prompt);
      QKeySequenceEdit* editor= new QKeySequenceEdit (&dialog);
      if (!shortcut->text ().trimmed ().isEmpty ())
        editor->setKeySequence (to_qkeysequence (tm_string (shortcut->text ().trimmed ())));
      layout->addWidget (editor);
      QDialogButtonBox* buttons= new QDialogButtonBox (
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
      connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
      connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
      layout->addWidget (buttons);
      editor->setFocus ();
      if (dialog.exec () == QDialog::Accepted)
        shortcut->setText (athena_shortcut_from_qt (editor->keySequence ()));
    });
    connect (apply, &QPushButton::clicked, this, [this] () { applyCurrent (); });
    connect (remove, &QPushButton::clicked, this, [this] () { removeCurrent (); });

    refreshList (initialShortcut);
    shortcut->setText (initialShortcut);
    command->setText (initialCommand);
  }

  void refreshList (const QString& selected= QString ()) {
    list->clear ();
    for (auto it= entries.constBegin (); it != entries.constEnd (); ++it) {
      QListWidgetItem* item= new QListWidgetItem (native_shortcut_label (it.key ()), list);
      item->setData (Qt::UserRole, it.key ());
      item->setToolTip (it.key ());
      if (it.key () == selected) list->setCurrentItem (item);
    }
  }

  void applyCurrent () {
    QString shown= shortcut->text ().trimmed ();
    QString cmd= command->text ().trimmed ();
    if (shown.isEmpty () || cmd.isEmpty ()) return;
    if (!selectedShortcut.isEmpty () && selectedShortcut != shown)
      entries.remove (selectedShortcut);
    entries.insert (shown, cmd);
    selectedShortcut= shown;
    refreshList (shown);
  }

  void removeCurrent () {
    QString shown= shortcut->text ().trimmed ();
    if (shown.isEmpty () && selectedShortcut.isEmpty ()) return;
    entries.remove (selectedShortcut.isEmpty () ? shown : selectedShortcut);
    selectedShortcut.clear ();
    shortcut->clear ();
    command->clear ();
    refreshList ();
  }

  QStringList result () const {
    QStringList out;
    for (auto it= entries.constBegin (); it != entries.constEnd (); ++it)
      out << it.key () << it.value ();
    return out;
  }

  QMap<QString,QString> entries;
  QString selectedShortcut;
  QListWidget* list= nullptr;
  QLineEdit* shortcut= nullptr;
  QLineEdit* command= nullptr;
};

struct ShortcutRequest {
  QString shortcut;
  QString command;
  QStringList entries;
  QStringList result;
  bool accepted= false;
};

} // namespace

void
qtm_info_dialog (string message, string title) {
  QString qMessage= qs (message), qTitle= qs (title);
  invoke_gui_blocking ([qMessage, qTitle] () {
    QMessageBox box (QApplication::activeWindow ());
    box.setWindowTitle (qTitle);
    box.setText (qMessage);
    box.setTextFormat (Qt::PlainText);
    box.setIcon (QMessageBox::Information);
    box.setStandardButtons (QMessageBox::Ok);
    box.setMinimumWidth (560);
    for (QLabel* label: box.findChildren<QLabel*> ()) label->setWordWrap (true);
    if (QGridLayout* layout= qobject_cast<QGridLayout*> (box.layout ())) {
      QSpacerItem* spacer=
        new QSpacerItem (520, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
      layout->addItem (spacer, layout->rowCount (), 0, 1,
                       layout->columnCount ());
    }
    box.exec ();
  });
}

static std::vector<QtInteractiveField>
interactive_fields (const std::vector<QTMInteractiveField>& fields) {
  std::vector<QtInteractiveField> qtFields;
  qtFields.reserve (fields.size ());
  for (const QTMInteractiveField& field: fields) {
    QtInteractiveField qtField;
    qtField.prompt= qs (field.prompt);
    qtField.type= qs (field.type);
    for (int i=0; i<N(field.proposals); ++i)
      qtField.proposals << qs (field.proposals[i]);
    qtFields.push_back (std::move (qtField));
  }
  return qtFields;
}

static QVector<QWidget*>
populate_interactive_form (QDialog& dialog,
                           const std::vector<QtInteractiveField>& fields) {
  QVBoxLayout* outer= new QVBoxLayout (&dialog);
  QFormLayout* form= new QFormLayout ();
  QVector<QWidget*> editors;
  for (const QtInteractiveField& field: fields) {
    QWidget* editor= interactive_field_widget (field, &dialog);
    editors << editor;
    QLabel* label= new QLabel (field.prompt, &dialog);
    label->setBuddy (editor);
    form->addRow (label, editor);
  }
  outer->addLayout (form);
  QDialogButtonBox* buttons= new QDialogButtonBox (
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
    Qt::Horizontal, &dialog);
  QObject::connect (buttons, &QDialogButtonBox::accepted,
                    &dialog, &QDialog::accept);
  QObject::connect (buttons, &QDialogButtonBox::rejected,
                    &dialog, &QDialog::reject);
  outer->addWidget (buttons);
  if (!editors.isEmpty ()) editors[0]->setFocus (Qt::OtherFocusReason);
  return editors;
}

void
qtm_interactive_form_async (
    string title, const std::vector<QTMInteractiveField>& fields,
    std::function<void(std::vector<std::string>)> completion) {
  auto* app= QCoreApplication::instance ();
  if (app == nullptr) return;
  QString qTitle= qs (title);
  auto qtFields= interactive_fields (fields);
  QMetaObject::invokeMethod (app,
    [qTitle, qtFields= std::move (qtFields),
     completion= std::move (completion)] () {
      auto* dialog= new QDialog (QApplication::activeWindow ());
      dialog->setWindowTitle (qTitle);
      dialog->setWindowModality (Qt::ApplicationModal);
      auto editors= populate_interactive_form (*dialog, qtFields);
      QObject::connect (dialog, &QDialog::finished, dialog,
        [dialog, editors, completion] (int status) {
          std::vector<std::string> result;
          if (status == QDialog::Accepted)
            for (QWidget* editor: editors)
              result.push_back (
                qs (interactive_field_value (editor)).toUtf8 ().toStdString ());
          dialog->deleteLater ();
          completion (std::move (result));
        });
      dialog->show ();
    }, Qt::QueuedConnection);
}

array<string>
qtm_interactive_dialog (
    string title, const std::vector<QTMInteractiveField>& fields) {
  QString qTitle= qs (title);
  auto qtFields= interactive_fields (fields);
  auto result= std::make_shared<QStringList> ();
  invoke_gui_blocking ([qTitle, qtFields, result] () {
    if (qtFields.size () == 1 &&
        qtFields[0].type == QStringLiteral ("question")) {
      int selected= question_dialog (qtFields[0]);
      if (selected >= 0 && selected < qtFields[0].proposals.size ())
        *result << qtFields[0].proposals[selected];
      return;
    }

    QDialog dialog (QApplication::activeWindow ());
    dialog.setWindowTitle (qTitle);
    auto editors= populate_interactive_form (dialog, qtFields);
    if (dialog.exec () != QDialog::Accepted) return;
    for (QWidget* editor: editors) *result << qs (interactive_field_value (editor));
  });
  array<string> out;
  for (const QString& value: *result) out << tm_string (value);
  return out;
}

array<string>
qtm_color_dialog (string title, array<string> recent_values,
                  array<string> saved_values) {
  QString qTitle= qs (title);
  QStringList recentNames;
  QStringList savedNames;
  for (int i=0; i<N(recent_values); ++i) recentNames << qs (recent_values[i]);
  for (int i=0; i<N(saved_values); ++i) savedNames << qs (saved_values[i]);
  auto result= std::make_shared<QStringList> ();
  invoke_gui_blocking ([qTitle, recentNames, savedNames, result] () {
    QList<QColor> recent;
    QSet<QString> seen;
    for (const QString& name: recentNames) {
      QColor color (name);
      QString canonical= color.name ();
      if (color.isValid () && !seen.contains (canonical)) {
        recent << color;
        seen.insert (canonical);
      }
    }

    for (int i=0; i<QColorDialog::customCount (); ++i)
      QColorDialog::setCustomColor (i, Qt::transparent);
    int custom= 0;
    for (const QString& name: savedNames) {
      if (custom >= QColorDialog::customCount ()) break;
      QColor color (name);
      if (color.isValid ()) QColorDialog::setCustomColor (custom++, color);
    }

    QColorDialog dialog (recent.isEmpty () ? Qt::white : recent.first (),
                         QApplication::activeWindow ());
    dialog.setWindowTitle (qTitle);
    dialog.setOption (QColorDialog::DontUseNativeDialog);
    if (!recent.isEmpty ()) {
      QWidget* recentWidget= new QWidget (&dialog);
      QHBoxLayout* layout= new QHBoxLayout (recentWidget);
      layout->setContentsMargins (6, 4, 6, 2);
      layout->setSpacing (4);
      layout->addWidget (new QLabel (QObject::tr ("Recent colors:"), recentWidget));
      for (const QColor& color: recent) {
        QToolButton* button= new QToolButton (recentWidget);
        button->setFixedSize (28, 28);
        button->setToolTip (color.name ());
        button->setStyleSheet (
          QString ("QToolButton { background-color: %1; border: 1px solid "
                   "#777; } QToolButton:hover { border: 2px solid #222; }")
            .arg (color.name ()));
        QObject::connect (button, &QToolButton::clicked, &dialog,
                          [&dialog, color] { dialog.setCurrentColor (color); });
        layout->addWidget (button);
      }
      layout->addStretch ();
      if (QVBoxLayout* dialogLayout=
            qobject_cast<QVBoxLayout*> (dialog.layout ()))
        dialogLayout->insertWidget (0, recentWidget);
      else
        dialog.layout ()->addWidget (recentWidget);
    }

    if (dialog.exec () != QDialog::Accepted || !dialog.selectedColor ().isValid ())
      return;
    *result << dialog.selectedColor ().name ();
    QSet<QString> savedSeen;
    for (int i=0; i<QColorDialog::customCount () && result->size () <= 8; ++i) {
      QColor color= QColorDialog::customColor (i);
      QString canonical= color.name ();
      if (color.isValid () && color.alpha () != 0 &&
          !savedSeen.contains (canonical)) {
        *result << canonical;
        savedSeen.insert (canonical);
      }
    }
  });
  array<string> out;
  for (const QString& value: *result) out << tm_string (value);
  return out;
}

void
qtm_print_file_dialog (url file) {
  QString fileName= qs (as_string (file));
  invoke_gui_blocking ([fileName] () {
    QTMPrinterSettings*& settings= printer_settings ();
    if (settings == nullptr) {
      QMessageBox::warning (nullptr, QObject::tr ("Print"),
        QObject::tr ("Direct printing is unavailable on this platform. Export to PDF and print it with your PDF viewer."));
      return;
    }
    settings->fileName= fileName;
    QTMPrintDialog dialog (settings);
    if (dialog.exec () != QDialog::Accepted) return;
    qt_system (tm_string (settings->toSystemCommand ()));
  });
}

array<SI>
qtm_tooltip_size (tree doc, tree style) {
  widget wid= texmacs_output_widget (doc, style);
  return get_texmacs_widget_size (wid);
}

void
qtm_tooltip_show (tree doc, tree style, int x, int y) {
  auto request= std::make_shared<TooltipRequest> ();
  request->doc= copy (doc);
  request->style= copy (style);
  request->x= x;
  request->y= y;
  invoke_gui_blocking ([request] () {
    if (tooltip_window_handle != 0) window_delete (tooltip_window_handle);
    widget wid= texmacs_output_widget (request->doc, request->style);
    tooltip_window_handle= window_handle ();
    window_create_tooltip (tooltip_window_handle, wid, "Tooltip");
    window_set_position (tooltip_window_handle, request->x, request->y);
    window_show (tooltip_window_handle);
  });
}

void
qtm_tooltip_close () {
  invoke_gui_blocking ([] () {
    if (tooltip_window_handle == 0) return;
    window_hide (tooltip_window_handle);
    window_delete (tooltip_window_handle);
    tooltip_window_handle= 0;
  });
}

string
qtm_linked_file_choice_dialog (string name, array<string> items) {
  auto request= std::make_shared<LinkedFileRequest> ();
  request->name= qs (name);
  for (int i=0; i<N(items); ++i) request->items << qs (items[i]);
  invoke_gui_blocking ([request] () {
    LinkedFileChoiceDialog dialog (request->name, request->items,
                                   QApplication::activeWindow ());
    if (dialog.exec () == QDialog::Accepted) request->result= dialog.result;
  });
  return tm_string (request->result);
}

array<string>
qtm_unsaved_buffers_dialog (array<string> buffers, bool restart) {
  auto request= std::make_shared<UnsavedRequest> ();
  request->restart= restart;
  for (int i=0; i<N(buffers); ++i) request->buffers << qs (buffers[i]);
  invoke_gui_blocking ([request] () {
    UnsavedBuffersDialog dialog (request->buffers, request->restart,
                                 QApplication::activeWindow ());
    if (dialog.exec () == QDialog::Accepted) {
      request->action= dialog.action;
      request->selected= dialog.selectedBuffers ();
    }
    else request->action= "cancel";
  });
  array<string> result;
  result << tm_string (request->action);
  for (const QString& value: request->selected) result << tm_string (value);
  return result;
}

void
qtm_text_report_dialog (string title, string message) {
  QString qTitle= qs (title), qMessage= qs (message);
  invoke_gui_blocking ([qTitle, qMessage] () {
    TextReportDialog dialog (qTitle, qMessage, QApplication::activeWindow ());
    dialog.exec ();
  });
}

array<string>
qtm_latex_formula_dialog () {
  auto result= std::make_shared<QStringList> ();
  invoke_gui_blocking ([result] () {
    LatexFormulaDialog dialog (QApplication::activeWindow ());
    if (dialog.exec () == QDialog::Accepted)
      *result << dialog.mode->currentText () << dialog.formula->toPlainText ();
  });
  array<string> out;
  for (const QString& value: *result) out << tm_string (value);
  return out;
}

array<string>
qtm_background_selector_dialog (string mode, array<string> initial) {
  auto request= std::make_shared<BackgroundRequest> ();
  request->mode= qs (mode);
  for (int i=0; i<N(initial); ++i) request->initial << qs (initial[i]);
  invoke_gui_blocking ([request] () {
    BackgroundSelectorDialog dialog (request->mode, request->initial,
                                     QApplication::activeWindow ());
    if (dialog.exec () == QDialog::Accepted) request->result= dialog.result ();
  });
  array<string> out;
  for (const QString& value: request->result) out << tm_string (value);
  return out;
}

array<string>
qtm_shortcut_editor_dialog (string initial_shortcut, string initial_command,
                            array<string> entries) {
  auto request= std::make_shared<ShortcutRequest> ();
  request->shortcut= qs (initial_shortcut);
  request->command= qs (initial_command);
  for (int i=0; i<N(entries); ++i) request->entries << qs (entries[i]);
  invoke_gui_blocking ([request] () {
    ShortcutEditorDialog dialog (request->shortcut, request->command,
                                 request->entries,
                                 QApplication::activeWindow ());
    if (dialog.exec () == QDialog::Accepted) {
      request->accepted= true;
      request->result= dialog.result ();
    }
  });
  array<string> out;
  if (!request->accepted) return out;
  out << string ("accepted");
  for (const QString& value: request->result) out << tm_string (value);
  return out;
}
