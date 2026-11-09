// Slide transition and object animation controls. GPL-3.0-or-later.
#include "UI/Qt/Document/QTMSlideMotionProperties.hpp"
#include "UI/Qt/Toolkit/qt_utilities.hpp"
#include "url.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace motion= athena::slides::motion;
namespace {
QComboBox* choices (QWidget* parent, std::initializer_list<std::pair<const char*, const char*>> values,
                    const std::string& selected) {
  auto* box= new QComboBox (parent);
  for (auto [label, value]: values) box->addItem (QObject::tr (label), value);
  box->setCurrentIndex (box->findData (QString::fromStdString (selected)));
  return box;
}
QComboBox* direction (QWidget* p, const std::string& value) {
  return choices (p, {{"Left", "left"}, {"Right", "right"}, {"Up", "up"}, {"Down", "down"}}, value);
}
QSpinBox* milliseconds (QWidget* parent, int value) {
  auto* box= new QSpinBox (parent);
  box->setRange (1, 600000);
  box->setSingleStep (50);
  box->setSuffix (" ms");
  box->setValue (value);
  return box;
}
std::string selected (QComboBox* box) { return box->currentData ().toString ().toStdString (); }
}
QIcon slide_motion_icon (const char* name) {
  return QIcon (to_qstring (concretize (url (string ("$ATHENA_PATH/misc/icons/libreoffice/colibre/") * name))));
}
struct QTMSlideMotionProperties::controls {
  QComboBox *transition= nullptr, *direction= nullptr;
  QSpinBox* duration= nullptr;
  struct row {
    QWidget* widget;
    QComboBox *frame, *category, *type, *direction;
    QSpinBox* duration;
  };
  std::vector<row> rows;
};
QTMSlideMotionProperties::~QTMSlideMotionProperties ()= default;
QTMSlideMotionProperties::QTMSlideMotionProperties (
  const tree& header, const std::vector<motion::event_frame>& frames, QWidget* parent):
  QWidget (parent), d (std::make_unique<controls> ()) {
  auto* layout= new QVBoxLayout (this);
  layout->setContentsMargins (0, 0, 0, 0);
  if (L(header) == SLIDE_PAGE) {
    const auto value= motion::read_transition (header);
    auto* group= new QGroupBox (tr ("Transition"), this);
    auto* form= new QFormLayout (group);
    d->transition= choices (group, {{"None", "none"}, {"Push", "push"}, {"Fade", "fade"}, {"Cube", "cube"}}, value.type);
    const char* icons[]= {"sd/cmd/transition-none.svg", "sd/cmd/transition-push.svg", "sd/cmd/transition-fade.svg", "sd/cmd/transition-cube-turning.svg"};
    for (int i=0; i<4; ++i) d->transition->setItemIcon (i, slide_motion_icon (icons[i]));
    d->direction= direction (group, value.direction);
    d->duration= milliseconds (group, value.duration);
    form->addRow (tr ("Effect:"), d->transition);
    form->addRow (tr ("Direction:"), d->direction);
    form->addRow (tr ("Duration:"), d->duration);
    auto changed= [this, form] {
      const auto effect= selected (d->transition);
      form->setRowVisible (d->direction, effect == "push" || effect == "cube");
      form->setRowVisible (d->duration, effect != "none");
    };
    connect (d->transition, &QComboBox::currentIndexChanged, this, changed);
    changed ();
    layout->addWidget (group);
    return;
  }
  auto* group= new QGroupBox (tr ("Animations"), this);
  auto* rows= new QVBoxLayout (group);
  auto* add= new QToolButton (group);
  add->setIcon (QIcon::fromTheme ("list-add"));
  add->setToolTip (tr ("Associate event frame"));
  rows->addWidget (add, 0, Qt::AlignLeft);
  const auto append= [this, frames, rows, group] (const motion::effect& value) {
    auto* widget= new QWidget (group);
    auto* form= new QFormLayout (widget);
    controls::row r;
    r.widget= widget;
    r.frame= new QComboBox (widget);
    for (size_t i=0; i<frames.size (); ++i)
      r.frame->addItem (QString ("%1. %2").arg (i+1).arg (QString::fromStdString (frames[i].name)), QString::fromStdString (frames[i].id));
    r.frame->setCurrentIndex (r.frame->findData (QString::fromStdString (value.frame)));
    r.category= choices (widget, {{"Entrance", "entrance"}, {"Emphasis", "emphasis"}, {"Exit", "exit"}}, value.category);
    r.type= new QComboBox (widget);
    r.direction= direction (widget, value.direction);
    r.duration= milliseconds (widget, value.duration);
    auto* remove= new QToolButton (widget);
    remove->setIcon (QIcon::fromTheme ("list-remove"));
    remove->setToolTip (tr ("Remove association"));
    auto* frameRow= new QHBoxLayout;
    frameRow->addWidget (r.frame, 1); frameRow->addWidget (remove);
    form->addRow (tr ("Event frame:"), frameRow);
    form->addRow (tr ("Category:"), r.category);
    form->addRow (tr ("Effect:"), r.type);
    form->addRow (tr ("From / toward:"), r.direction);
    form->addRow (tr ("Duration:"), r.duration);
    auto categoryChanged= [r] {
      auto previous= r.type->currentData ();
      r.type->clear ();
      if (selected (r.category) == "emphasis")
        r.type->addItem (slide_motion_icon ("sd/res/effectshape_16.svg"), tr ("Grow and return"), "grow");
      else {
        r.type->addItem (slide_motion_icon ("sd/cmd/transition-cut.svg"), selected (r.category) == "exit" ? tr ("Disappear") : tr ("Appear"), "appear");
        r.type->addItem (slide_motion_icon ("sd/cmd/transition-fade.svg"), tr ("Fade"), "fade");
        r.type->addItem (slide_motion_icon ("sd/cmd/transition-push.svg"), tr ("Fly"), "fly");
      }
      int index= r.type->findData (previous);
      if (index >= 0) r.type->setCurrentIndex (index);
    };
    connect (r.category, &QComboBox::currentIndexChanged, this, categoryChanged);
    auto typeChanged= [r, form] {
      form->setRowVisible (r.direction, selected (r.type) == "fly");
      form->setRowVisible (r.duration, selected (r.type) != "appear");
    };
    connect (r.type, &QComboBox::currentIndexChanged, this, typeChanged);
    categoryChanged ();
    r.type->setCurrentIndex (r.type->findData (QString::fromStdString (value.type)));
    typeChanged ();
    connect (remove, &QToolButton::clicked, this, [this, widget] {
      d->rows.erase (std::remove_if (d->rows.begin (), d->rows.end (),
        [widget] (const auto& r) { return r.widget == widget; }), d->rows.end ());
      widget->deleteLater ();
    });
    d->rows.push_back (r);
    rows->insertWidget (rows->count ()-1, widget);
  };
  for (const auto& effect: motion::read_effects (header)) append (effect);
  add->setEnabled (!frames.empty ());
  if (frames.empty ()) rows->addWidget (new QLabel (tr ("No event frames"), group));
  connect (add, &QToolButton::clicked, this, [append, frames] {
    motion::effect e;
    e.frame= frames.front ().id;
    append (e);
  });
  layout->addWidget (group);
}
void QTMSlideMotionProperties::write (athena::node::metadata& m) const {
  if (d->transition) {
    motion::write_transition (m, {selected (d->transition), selected (d->direction), d->duration->value ()});
    return;
  }
  std::vector<motion::effect> result;
  for (const auto& r: d->rows)
    result.push_back ({selected (r.frame), selected (r.category), selected (r.type), selected (r.direction), r.duration->value ()});
  motion::write_effects (m, result);
}
