# Qt color-emoji metrics corruption after `QPainterPath::addText`

## Scope

ATHENA currently depends on Qt for native rendering of color/bitmap emoji fonts.
During investigation of supplementary-plane emoji rendering, Qt 6.11.1 was
observed to mutate its own font-engine/cache state after outline extraction is
attempted on a color emoji glyph.

This note records the upstream behavior. ATHENA does not attempt to repair or
renormalize the corrupted Qt metrics.

Observed environment:

- Qt 6.11.1
- Linux / openSUSE Tumbleweed
- `Noto Color Emoji`
- U+1FAE0 MELTING FACE (`🫠`)

Platform and backend specificity have not been established. The behavior was
also reproduced in a standalone Qt program using the offscreen platform plugin,
so it is not known to be Wayland-specific.

## Minimal reproducer

```cpp
#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QString>
#include <iostream>

static void
report (const char* tag, const QFont& font, const QString& text) {
  QFontMetricsF metrics (font);
  QRectF bounds= metrics.tightBoundingRect (text);
  std::cout << tag
            << " advance=" << metrics.horizontalAdvance (text)
            << " bounds-width=" << bounds.width () << '\n';
}

int
main (int argc, char** argv) {
  QApplication app (argc, argv);

  QFont font ("Noto Color Emoji");
  font.setPixelSize (12);
  QString text= QString::fromUcs4 (U"🫠");

  report ("before", font, text);

  QPainterPath path;
  path.addText (QPointF (0, 0), font, text);

  report ("after same font", font, text);

  QFont fresh ("Noto Color Emoji");
  fresh.setPixelSize (12);
  report ("after fresh font", fresh, text);
}
```

Observed output:

```text
before advance=12.75 bounds-width=12.75
after same font advance=3.75 bounds-width=0
after fresh font advance=3.75 bounds-width=0
```

`QPainterPath::addText()` produces no useful outline for this glyph. More
importantly, the call changes subsequent `QFontMetricsF` results not only for the
existing `QFont`, but also for a newly constructed font and metrics object in the
same process.

## Why this matters

The documented purpose of `QFontMetricsF::horizontalAdvance()` is to provide the
advance used to position subsequent text. After the `addText()` call above, that
advance no longer agrees with the glyph that `QPainter::drawText()` renders.
Consequently, an application can lay out following text too early even though
the emoji itself still paints at its original visual size.

For ATHENA this appears as overlapping text after a color emoji. The document
content and Unicode conversion remain correct; the disagreement is between
Qt's post-`addText()` layout metrics and Qt's native color-glyph rendering.

## ATHENA policy

ATHENA's RenderService supports text runs that cannot be represented by vector
outlines by replaying them as native Qt text. This is an application rendering
capability, not a workaround for the metric corruption described above.

ATHENA intentionally does not patch, cache around, rescale, or otherwise try to
repair Qt's font metrics after this behavior occurs. Any upstream workaround or
fix should be justified by Qt source analysis and, ideally, an upstream Qt bug
report/fix rather than by compensating application-side spacing constants.
