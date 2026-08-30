
/******************************************************************************
* MODULE     : QTMFileDialog.cpp
* DESCRIPTION: QT file choosers
* COPYRIGHT  : (C) 2009 David MICHEL
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMFileDialog.hpp"
#include <QPainter>
#include <QLineEdit>
#include <QIntValidator>
#include <QValidator>
#include <QMimeData>
#include <QUrl>
#include <QDrag>
#include <QGridLayout>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include "file.hpp"
#include "sys_utils.hpp"
#include "qt_utilities.hpp"
#include "qt_gui.hpp"
#include "analyze.hpp"
#include "image_files.hpp"

QTMFileDialog::QTMFileDialog (QWidget * parent, const QString & caption, 
                              const QString & directory, const QString & filter)
  : QDialog (parent) 
{
  setWindowTitle (caption);
  hbox= new QHBoxLayout (this);
  hbox->setContentsMargins (0, 0, 0, 0);
  file= new QMyFileDialog (0, caption, directory, filter);
  file->setOption(QFileDialog::DontUseNativeDialog, false);
  hbox->addWidget (file);
  setLayout (hbox);
  setAcceptDrops(true);
  connect(file, SIGNAL(accepted()), this, SLOT(accept()));
  connect(file, SIGNAL(finished(int)), this, SLOT(done(int)));
  connect(file, SIGNAL(rejected()), this, SLOT(reject()));
}

void QTMFileDialog::dragEnterEvent(QDragEnterEvent *event)
{
	event->acceptProposedAction();
}

void QTMFileDialog::dragMoveEvent(QDragMoveEvent *event)
{
	event->acceptProposedAction();
}

void QTMFileDialog::dropEvent(QDropEvent *event)
{
	const QMimeData *mimeData = event->mimeData();
	
	foreach (QString format, mimeData->formats()) {
		if (format == "text/uri-list") {
			file->selectFile(mimeData->urls().at(0).toLocalFile());
			break;
		}
	}
	
	event->acceptProposedAction();
}

void QTMFileDialog::dragLeaveEvent(QDragLeaveEvent *event)
{
	event->accept();
}


static QWidget*
simple_input (string s, QLineEdit* ledit, QWidget* parent= 0) {
  QWidget* widget= new QWidget (parent);
  QHBoxLayout* layout= new QHBoxLayout (widget);
  layout->setContentsMargins (0, 0, 0, 0);
  QLabel* label= new QLabel (to_qstring (s), parent);
  layout->addWidget (label);
  layout->addWidget (ledit);
  widget->setLayout (layout);
  return widget;
}

static bool
parse_positive_length (const QString& text, double& value, QString& unit) {
  QString s= text.trimmed ();
  if (s.isEmpty ()) return false;
  int i= 0;
  bool dot= false;
  if (s[i] == '+') i++;
  int start= i;
  for (; i < s.size (); ++i) {
    QChar c= s[i];
    if (c.isDigit ()) continue;
    if (c == '.' && !dot) {
      dot= true;
      continue;
    }
    break;
  }
  if (i == start) return false;
  bool ok= false;
  value= s.left (i).toDouble (&ok);
  if (!ok || value <= 0) return false;
  unit= s.mid (i);
  if (unit.isEmpty ()) unit= "pt";
  return true;
}

static QString
format_scaled_length (double value, const QString& unit) {
  return QString::number ((int) ceil (value)) + unit;
}

QTMImagePreview::QTMImagePreview (QWidget* parent)
  : QWidget (parent), wid_slider (NULL), hei_slider (NULL),
    updating_dims (false), natural_width_pt (0), natural_height_pt (0) {
  QRegularExpression rxpos("^[+]?([0-9]*[.])?[0-9]+([a-z]*|%)$");
  //we could explicitly list all accepted lengths...
  QValidator *validator1 = new QRegularExpressionValidator(rxpos, this);
  QRegularExpression rx("^[+-]?([0-9]*[.])?[0-9]+([a-z]*|%)$");
  QValidator *validator2 = new QRegularExpressionValidator(rx, this);
  QHBoxLayout* hbox= new QHBoxLayout (this);
  hbox->setContentsMargins (8, 8, 8, 8);
  hbox->setSpacing (12);

  image= new QLabel (this);
  image->setMinimumSize (160, 160);
  image->setAlignment (Qt::AlignCenter);
  hbox->addWidget (image, 0, Qt::AlignCenter);

  QGridLayout* grid= new QGridLayout ();
  grid->setContentsMargins (0, 0, 0, 0);
  grid->setHorizontalSpacing (6);
  grid->setVerticalSpacing (6);

  wid= new QLineEdit (this);
  wid->setValidator(validator1);
  QLabel* wid_label= new QLabel (to_qstring ("Width:"), this);
  wid_slider= new QSlider (Qt::Horizontal, this);
  wid_slider->setMinimum (1);
  grid->addWidget (wid_label, 0, 0);
  grid->addWidget (wid, 0, 1);
  grid->addWidget (wid_slider, 1, 0, 1, 2);
  connect(wid, SIGNAL(textEdited(const QString)), this, SLOT(widthEdited(const QString)));
  connect(wid_slider, SIGNAL(valueChanged(int)), this, SLOT(widthSliderChanged(int)));

  hei= new QLineEdit (this);
  hei->setValidator(validator1);
  QLabel* hei_label= new QLabel (to_qstring ("Height:"), this);
  hei_slider= new QSlider (Qt::Horizontal, this);
  hei_slider->setMinimum (1);
  grid->addWidget (hei_label, 2, 0);
  grid->addWidget (hei, 2, 1);
  grid->addWidget (hei_slider, 3, 0, 1, 2);
  connect(hei, SIGNAL(textEdited(const QString)), this, SLOT(heightEdited(const QString)));
  connect(hei_slider, SIGNAL(valueChanged(int)), this, SLOT(heightSliderChanged(int)));

  xps= new QLineEdit (this);
  xps->setValidator(validator2);  
  grid->addWidget (new QLabel (to_qstring ("X-position:"), this), 4, 0);
  grid->addWidget (xps, 4, 1);

  yps= new QLineEdit (this);
  yps->setValidator(validator2);
  grid->addWidget (new QLabel (to_qstring ("Y-position:"), this), 5, 0);
  grid->addWidget (yps, 5, 1);
  grid->setColumnStretch (1, 1);

  hbox->addLayout (grid, 1);
  setLayout (hbox);
  setMinimumWidth (390);
  setMaximumWidth (480);
  setImage (0);
}

void
QTMImagePreview::clear_dim(){
BEGIN_SLOT
END_SLOT    
};

void
QTMImagePreview::updateSliders () {
  if (natural_width_pt <= 0 || natural_height_pt <= 0) {
    wid_slider->setEnabled (false);
    hei_slider->setEnabled (false);
    return;
  }
  wid_slider->setEnabled (true);
  hei_slider->setEnabled (true);
  wid_slider->setMaximum (qMax (natural_width_pt * 4, natural_width_pt + 200));
  hei_slider->setMaximum (qMax (natural_height_pt * 4, natural_height_pt + 200));
  wid_slider->setValue (natural_width_pt);
  hei_slider->setValue (natural_height_pt);
}

void
QTMImagePreview::setPairedHeightFromWidth (const QString& text) {
  double value;
  QString unit;
  if (!parse_positive_length (text, value, unit) ||
      natural_width_pt <= 0 || natural_height_pt <= 0) return;
  hei->setText (format_scaled_length (value * natural_height_pt /
                                      natural_width_pt, unit));
  if (unit == "pt") {
    int w= (int) ceil (value);
    int h= (int) ceil (value * natural_height_pt / natural_width_pt);
    wid_slider->setValue (qBound (wid_slider->minimum (), w,
                                  wid_slider->maximum ()));
    hei_slider->setValue (qBound (hei_slider->minimum (), h,
                                  hei_slider->maximum ()));
  }
}

void
QTMImagePreview::setPairedWidthFromHeight (const QString& text) {
  double value;
  QString unit;
  if (!parse_positive_length (text, value, unit) ||
      natural_width_pt <= 0 || natural_height_pt <= 0) return;
  wid->setText (format_scaled_length (value * natural_width_pt /
                                      natural_height_pt, unit));
  if (unit == "pt") {
    int h= (int) ceil (value);
    int w= (int) ceil (value * natural_width_pt / natural_height_pt);
    hei_slider->setValue (qBound (hei_slider->minimum (), h,
                                  hei_slider->maximum ()));
    wid_slider->setValue (qBound (wid_slider->minimum (), w,
                                  wid_slider->maximum ()));
  }
}

void
QTMImagePreview::widthEdited (const QString& text) {
BEGIN_SLOT
  if (updating_dims) return;
  updating_dims= true;
  setPairedHeightFromWidth (text);
  updating_dims= false;
END_SLOT
}

void
QTMImagePreview::heightEdited (const QString& text) {
BEGIN_SLOT
  if (updating_dims) return;
  updating_dims= true;
  setPairedWidthFromHeight (text);
  updating_dims= false;
END_SLOT
}

void
QTMImagePreview::widthSliderChanged (int value) {
BEGIN_SLOT
  if (updating_dims || natural_width_pt <= 0 || natural_height_pt <= 0) return;
  updating_dims= true;
  wid->setText (QString::number (value) + "pt");
  hei->setText (QString::number ((int) ceil (value * natural_height_pt /
                                            natural_width_pt)) + "pt");
  hei_slider->setValue (qBound (hei_slider->minimum (),
                                (int) ceil (value * natural_height_pt /
                                            natural_width_pt),
                                hei_slider->maximum ()));
  updating_dims= false;
END_SLOT
}

void
QTMImagePreview::heightSliderChanged (int value) {
BEGIN_SLOT
  if (updating_dims || natural_width_pt <= 0 || natural_height_pt <= 0) return;
  updating_dims= true;
  hei->setText (QString::number (value) + "pt");
  wid->setText (QString::number ((int) ceil (value * natural_width_pt /
                                            natural_height_pt)) + "pt");
  wid_slider->setValue (qBound (wid_slider->minimum (),
                                (int) ceil (value * natural_width_pt /
                                            natural_height_pt),
                                wid_slider->maximum ()));
  updating_dims= false;
END_SLOT
}


void 
QTMImagePreview::setImage (const QString& file) { 	  //generate thumbnail
BEGIN_SLOT
  QImage img;
  updating_dims= true;
  natural_width_pt= 0;
  natural_height_pt= 0;
  wid->setText ("");
  hei->setText ("");
  xps->setText ("");
  yps->setText ("");

  string localname= from_qstring_utf8 (file);
  url image_url= url_system (localname);
  if (DEBUG_CONVERT) debug_convert<<"image preview :["<<image_url<<"]"<<LF;
  if (!(as_string(image_url)=="") && !is_directory(image_url) && exists(image_url) ){
    url temp= url_temp (".png");
    int w_pt, h_pt;
    double w, h;
    image_size (image_url, w_pt, h_pt);
    if (w_pt*h_pt !=0) { //necessary if gs returns h=v=0 (for instance 0-size pdf)
      natural_width_pt= w_pt;
      natural_height_pt= h_pt;
      wid->setText (QString::number (w_pt) + "pt");
      hei->setText (QString::number (h_pt) + "pt");
      if (w_pt > h_pt) {
        w= 158;
        h= ceil (h_pt*158/w_pt);
      } else {
        w= ceil (w_pt*158/h_pt);
        h= 158;
      }
      // generate thumbnail:
      image_to_png (image_url, temp, w, h);
      img.load (utf8_to_qstring (as_string (temp)));
      remove (temp);
    }
  }

  if (img.isNull()) {
    QImage vide (160, 160, QImage::Format_RGB32);
    QPainter painter;
    painter.begin (&vide);
    painter.fillRect (0, 0, 160, 160, Qt::white);
    QPen ThinBlack (Qt::black);
    ThinBlack.setWidth (0);
    ThinBlack.setStyle (Qt::SolidLine);
    painter.setPen (ThinBlack);
    painter.drawLine (0, 0, 159, 159);
    painter.drawLine (0, 159, 159, 0);
    painter.drawRect (0, 0, 159, 159);
    painter.end ();
    image->setPixmap(QPixmap::fromImage(vide));
  }
  else
    image->setPixmap (QPixmap::fromImage (img.scaled (158, 158, Qt::KeepAspectRatio, Qt::FastTransformation)));
  updateSliders ();
  updating_dims= false;
END_SLOT
}

QTMImageDialog::QTMImageDialog (QWidget* parent, const QString& caption, const QString& directory, const QString& filter)
  : QTMFileDialog (parent, caption, directory, filter)
{
  preview= new QTMImagePreview (this);
  hbox->insertWidget(0, preview);
  connect(file, SIGNAL(currentChanged (const QString&)), preview, SLOT(setImage(const QString&)));
}

string
QTMImageDialog::getParamsAsString () {
  string params;
  params << "\"" << from_qstring (preview->wid->text ()) << "\" ";
  params << "\"" << from_qstring (preview->hei->text ()) << "\" ";
  params << "\"" << from_qstring (preview->xps->text ()) << "\" ";
  params << "\"" << from_qstring (preview->yps->text ()) << "\"";
  return params;
}
