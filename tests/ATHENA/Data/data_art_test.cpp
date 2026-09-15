/******************************************************************************
* MODULE     : data_art_test.cpp
* DESCRIPTION: tests for native VTK DataArt cover generation
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include <QtTest/QtTest>

#include "ATHENA/Data/data_art.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

namespace {

QByteArray
file_bytes (const QString& path) {
  QFile file (path);
  if (!file.open (QIODevice::ReadOnly)) return {};
  return file.readAll ();
}

QRect
surface_bounds (const QImage& image) {
  if (image.isNull ()) return {};
  const QColor background= image.pixelColor (0, 0);
  int min_x= image.width ();
  int min_y= image.height ();
  int max_x= -1;
  int max_y= -1;
  constexpr int tolerance= 4;
  for (int y=0; y<image.height (); ++y)
    for (int x=0; x<image.width (); ++x) {
      const QColor pixel= image.pixelColor (x, y);
      const int delta= std::max ({
        std::abs (pixel.red () - background.red ()),
        std::abs (pixel.green () - background.green ()),
        std::abs (pixel.blue () - background.blue ())});
      if (delta <= tolerance) continue;
      min_x= std::min (min_x, x);
      min_y= std::min (min_y, y);
      max_x= std::max (max_x, x);
      max_y= std::max (max_y, y);
    }
  if (max_x < min_x || max_y < min_y) return {};
  return QRect (QPoint (min_x, min_y), QPoint (max_x, max_y));
}

} // namespace

class TestDataArt: public QObject {
  Q_OBJECT

private slots:
  void rendersAllSurfaceFamiliesHeadlessly ();
  void repeatsTheSameSeedDeterministically ();
};

void
TestDataArt::rendersAllSurfaceFamiliesHeadlessly () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const char* seeds[] {
    "ATHENA native DataArt reference 0", // Fourier
    "ATHENA native DataArt reference 2", // radial
    "ATHENA native DataArt reference 4"  // algebraic
  };

  QByteArray previous_hash;
  for (int i=0; i<3; ++i) {
    const QString path= temporary.filePath (QString ("cover-%1.png").arg (i));
    const QByteArray path_bytes= path.toUtf8 ();
    const string error= athena_data_art_generate (
      string (seeds[i]),
      url_system (string (path_bytes.constData (), path_bytes.size ())));
    QVERIFY2 (error == "", as_charp (error));

    const QByteArray bytes= file_bytes (path);
    QVERIFY (bytes.size () > 24);
    QCOMPARE (bytes.left (8), QByteArray::fromHex ("89504e470d0a1a0a"));
    const QImage image (path);
    QVERIFY (!image.isNull ());
    const QRect bounds= surface_bounds (image);
    QVERIFY (!bounds.isEmpty ());
    QVERIFY2 ((double) bounds.width () / image.width () >= 0.85,
              qPrintable (QString ("surface width occupancy %1/%2")
                .arg (bounds.width ()).arg (image.width ())));
    QVERIFY2 ((double) bounds.height () / image.height () >= 0.85,
              qPrintable (QString ("surface height occupancy %1/%2")
                .arg (bounds.height ()).arg (image.height ())));
    const QByteArray hash= QCryptographicHash::hash (
      bytes, QCryptographicHash::Sha256);
    if (!previous_hash.isEmpty ()) QVERIFY (hash != previous_hash);
    previous_hash= hash;
  }
}

void
TestDataArt::repeatsTheSameSeedDeterministically () {
  QTemporaryDir temporary;
  QVERIFY (temporary.isValid ());
  const QByteArray seed= "ATHENA deterministic native DataArt";
  QByteArray hashes[2];
  for (int i=0; i<2; ++i) {
    const QString path= temporary.filePath (QString ("repeat-%1.png").arg (i));
    const QByteArray path_bytes= path.toUtf8 ();
    const string error= athena_data_art_generate (
      string (seed.constData (), seed.size ()),
      url_system (string (path_bytes.constData (), path_bytes.size ())));
    QVERIFY2 (error == "", as_charp (error));
    hashes[i]= QCryptographicHash::hash (
      file_bytes (path), QCryptographicHash::Sha256);
  }
  QCOMPARE (hashes[0], hashes[1]);
}

QTEST_APPLESS_MAIN (TestDataArt)
#include "data_art_test.moc"
