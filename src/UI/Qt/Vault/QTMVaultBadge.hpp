// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <cmath>

enum { VaultBadgeRole= Qt::UserRole + 60, VaultHighlightRole };

inline void vault_kind_badge (QListWidgetItem* item, QString kind) {
  if (kind.isEmpty () || kind == "heading") return;
  kind[0]= kind[0].toUpper ();
  item->setData (VaultBadgeRole, kind);
}

inline void vault_file_badge (QListWidgetItem* item) {
  const QString ext= item->text ().section ('.', -1).toUpper ();
  if (ext == "ATH" || ext == "AVD") item->setData (VaultBadgeRole, ext);
}

inline QColor vault_badge_color (const QString& label) {
  if (label == "ATH") return QColor ("#007987");
  if (label == "AVD") return QColor ("#7936d5");
  if (label == "Theorem") return QColor ("#254bc9");
  if (label == "Lemma") return QColor ("#7735c7");
  if (label == "Proposition") return QColor ("#006baf");
  if (label == "Corollary") return QColor ("#ad2675");
  if (label == "Definition") return QColor ("#b34605");
  if (label == "Proof") return QColor ("#08763f");
  static const char* const colors[]= {
    "#bd2340", "#1b65b3", "#a13b78", "#067462", "#974508", "#6041c1"
  };
  return QColor (colors[qHash (label, 0) % 6]);
}

class VaultBadgeDelegate final: public QStyledItemDelegate {
public:
  VaultBadgeDelegate (QListWidget* list, QLineEdit* input, QCheckBox* ci, QCheckBox* fuzzy):
    QStyledItemDelegate (list), input (input), ci (ci), fuzzy (fuzzy) {
    if (input) QObject::connect (input, &QLineEdit::textChanged, list->viewport (),
                                 qOverload<> (&QWidget::update));
  }

  QSize sizeHint (const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    return drawRow (nullptr, option, index);
  }

  void paint (QPainter* painter, const QStyleOptionViewItem& option,
              const QModelIndex& index) const override {
    drawRow (painter, option, index);
  }

private:
  QLineEdit* input;
  QCheckBox *ci, *fuzzy;

  QList<QTextLayout::FormatRange> matches (const QString& text) const {
    QList<QTextLayout::FormatRange> ranges;
    const auto query= input->text ().trimmed ();
    if (query.isEmpty ()) return ranges;
    const auto cs= ci && !ci->isChecked () ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QTextCharFormat bold;
    bold.setFontWeight (QFont::Bold);
    int pos= 0;
    while ((pos= text.indexOf (query, pos, cs)) >= 0) {
      ranges.append ({pos, int (query.size ()), bold});
      pos+= query.size ();
    }
    if (ranges.isEmpty () && fuzzy && fuzzy->isChecked ()) {
      pos= 0;
      for (QChar c: query) {
        pos= text.indexOf (c, pos, cs);
        if (pos < 0) return {};
        ranges.append ({pos++, 1, bold});
      }
    }
    return ranges;
  }

  QSize drawRow (QPainter* painter, QStyleOptionViewItem opt, const QModelIndex& index) const {
    initStyleOption (&opt, index);
    const QString badgeText= index.data (VaultBadgeRole).toString ();
    QString text= opt.text;
    if (!badgeText.isEmpty () && text.startsWith (badgeText + ":", Qt::CaseInsensitive))
      text= text.sliced (badgeText.size () + 1).trimmed ();
    else if (text.compare (badgeText, Qt::CaseInsensitive) == 0) text.clear ();
    QTextLayout layout (text, opt.font);
    layout.setFormats (input ? matches (text) :
      index.data (VaultHighlightRole).value<QList<QTextLayout::FormatRange>> ());
    layout.beginLayout ();
    auto line= layout.createLine ();
    layout.endLayout ();
    QFont badgeFont= opt.font;
    badgeFont.setPixelSize (qMax (9, qRound (opt.fontMetrics.height () * 0.72)));
    auto badgeFormats= input ? matches (badgeText) : QList<QTextLayout::FormatRange> ();
    badgeFont.setWeight (badgeFormats.isEmpty () ? QFont::Bold : QFont::Normal);
    QTextLayout badgeLayout (badgeText, badgeFont);
    badgeLayout.setFormats (badgeFormats);
    badgeLayout.beginLayout ();
    auto badgeLine= badgeLayout.createLine ();
    badgeLayout.endLayout ();
    QFontMetrics fm (badgeFont);
    const int pad= qMax (3, fm.height () / 4);
    const int badgeWidth= qCeil (badgeLine.naturalTextWidth ()) + 2*pad;
    const int indent= !badgeText.isEmpty () ? badgeWidth + 2*pad : 0;
    if (painter) {
      const auto* style= opt.widget ? opt.widget->style () : QApplication::style ();
      opt.text.clear ();
      style->drawControl (QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
      painter->save ();
      painter->setClipRect (opt.rect, Qt::IntersectClip);
      const int x= opt.rect.left () + pad;
      if (!badgeText.isEmpty ()) {
        QRect badge (x, opt.rect.top () + (opt.rect.height ()-fm.height ())/2,
                     badgeWidth, fm.height ());
        painter->setRenderHint (QPainter::Antialiasing);
        painter->setPen (Qt::NoPen);
        painter->setBrush (vault_badge_color (badgeText));
        painter->drawRoundedRect (badge, 3, 3);
        painter->setPen (Qt::white);
        painter->setFont (badgeFont);
        badgeLayout.draw (painter, QPointF (badge.left () + pad,
          badge.top () + (badge.height () - badgeLine.height ()) / 2));
      }
      painter->setPen (opt.palette.color (opt.state & QStyle::State_Selected ?
                                         QPalette::HighlightedText : QPalette::Text));
      layout.draw (painter, QPointF (x+indent, opt.rect.top ()+(opt.rect.height ()-line.height ())/2));
      painter->restore ();
    }
    return {int (std::ceil (line.naturalTextWidth ())) + indent + 2*pad,
            qMax (int (std::ceil (line.height ())), fm.height ()) + pad};
  }
};

inline void install_vault_badges (QListWidget* list, QLineEdit* input= nullptr,
                                  QCheckBox* ci= nullptr, QCheckBox* fuzzy= nullptr) {
  list->setItemDelegate (new VaultBadgeDelegate (list, input, ci, fuzzy));
}
