/******************************************************************************
* MODULE     : handwriting_recognizer_test.cpp
* DESCRIPTION: Tests for Hand TeX handwriting recognition
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "ATHENA/Math/handwriting_recognizer.hpp"

#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

class TestHandwritingRecognizer: public QObject {
  Q_OBJECT

private slots:
  void preprocessingIsCenteredAndDeduplicated ();
  void packagedModelProducesCandidates ();
};

void
TestHandwritingRecognizer::preprocessingIsCenteredAndDeduplicated () {
  athena_handwriting_stroke line= {{20.0f, 50.0f}, {180.0f, 50.0f}};
  std::vector<float> once= athena_handwriting_recognizer::preprocess (
    {line}, 200.0f, 100.0f);
  std::vector<float> duplicated= athena_handwriting_recognizer::preprocess (
    {line, line}, 200.0f, 100.0f);

  QCOMPARE (once.size (), (size_t) 64 * 64);
  QCOMPARE (once, duplicated);
  QCOMPARE (*std::min_element (once.begin (), once.end ()), -1.0f);
  QCOMPARE (*std::max_element (once.begin (), once.end ()), 1.0f);

  size_t black_pixels= (size_t) std::count (once.begin (), once.end (), -1.0f);
  QVERIFY (black_pixels >= 56 && black_pixels <= 60);
  for (int y=0; y<64; y++)
    if (y < 31 || y > 33)
      for (int x=0; x<64; x++) QCOMPARE (once[(size_t) y * 64 + x], 1.0f);
}

void
TestHandwritingRecognizer::packagedModelProducesCandidates () {
  const char* athena_path= std::getenv ("ATHENA_PATH");
  QVERIFY2 (athena_path != nullptr, "ATHENA_PATH must be set for this test");
  std::filesystem::path assets= std::filesystem::path (athena_path) /
    "misc/models/handwriting";
  athena_handwriting_recognizer recognizer (assets.string ());

  std::string error;
  QVERIFY2 (recognizer.available (error), error.c_str ());
  athena_handwriting_stroke line= {{20.0f, 50.0f}, {180.0f, 50.0f}};
  auto predictions= recognizer.recognize ({line}, 200.0f, 100.0f, 8, error);
  QVERIFY2 (error.empty (), error.c_str ());
  QVERIFY (!predictions.empty ());
  QVERIFY (predictions.size () <= 8);
  QCOMPARE (predictions.front ().key, std::string ("latex2e-_--"));
  QCOMPARE (predictions.front ().command, std::string ("\\--"));
  QVERIFY (predictions.front ().confidence > 0.99f);
  for (size_t i=0; i<predictions.size (); i++) {
    QVERIFY (!predictions[i].key.empty ());
    QVERIFY (!predictions[i].command.empty ());
    QVERIFY (std::isfinite (predictions[i].confidence));
    QVERIFY (predictions[i].confidence >= 0.0f &&
             predictions[i].confidence <= 1.0f);
    if (i > 0)
      QVERIFY (predictions[i - 1].confidence >= predictions[i].confidence);
  }
}

QTEST_MAIN(TestHandwritingRecognizer)
#include "handwriting_recognizer_test.moc"
