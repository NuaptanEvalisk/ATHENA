/******************************************************************************
* MODULE     : aofm_chatgpt_test.cpp
* DESCRIPTION: Tests for ChatGPT clipboard Markdown repair
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include <QtTest/QtTest>

#include "convert.hpp"

static int
countOccurrences (const std::string& text, const std::string& needle) {
  int count= 0;
  for (size_t at= 0; (at= text.find (needle, at)) != std::string::npos;
       at += needle.size ())
    ++count;
  return count;
}

class TestAofmChatGpt: public QObject {
  Q_OBJECT

private slots:
  void restoresSeparateBalancedInlineFormulas ();
  void restoresNestedAndAtomicInlineFormulas ();
  void leavesProseLinksAndCodeAlone ();
  void restoresStandaloneDisplayBlocks ();
  void normalizesReportedSample ();
};

void
TestAofmChatGpt::restoresSeparateBalancedInlineFormulas () {
  QCOMPARE (aofm_normalize_chatgpt_markdown (
              "(x^2) and (x^3 + 1)"),
            std::string ("\\(x^2\\) and \\(x^3 + 1\\)"));
}

void
TestAofmChatGpt::restoresNestedAndAtomicInlineFormulas () {
  QCOMPARE (aofm_normalize_chatgpt_markdown (
              "(\\det(A)\\neq 0), (x), (u), and (1)."),
            std::string ("\\(\\det(A)\\neq 0\\), \\(x\\), \\(u\\), "
                         "and \\(1\\)."));
}

void
TestAofmChatGpt::leavesProseLinksAndCodeAlone () {
  const std::string source=
    "Keep (for example), foo(x), [label](https://example.com), and `(x^2)`.\n"
    "Existing \\(y^2\\) and $z^2$ stay intact.\n"
    "```text\n(x^2)\n[\ny^2\n]\n```\n";
  QCOMPARE (aofm_normalize_chatgpt_markdown (source), source);
}

void
TestAofmChatGpt::restoresStandaloneDisplayBlocks () {
  const std::string source=
    "Inline [brackets] stay.\n\n[\nQ=\\begin{pmatrix}\n1 & 2\\\\\n"
    "3 & 4\n\\end{pmatrix}\n]\n";
  const std::string expected=
    "Inline [brackets] stay.\n\n\\[\nQ=\\begin{pmatrix}\n1 & 2\\\\\n"
    "3 & 4\n\\end{pmatrix}\n\\]\n";
  QCOMPARE (aofm_normalize_chatgpt_markdown (source), expected);
}

void
TestAofmChatGpt::normalizesReportedSample () {
  const std::string source= R"(Here are a few examples.

Inline mathematics: (e^{i\pi}+1=0), (a^2+b^2=c^2), (\int_0^1 x^2,dx=\frac13), and (\forall x\in\mathbb{R},\ x^2\ge 0).

Display equations:

[
\sum_{k=1}^{n} k = \frac{n(n+1)}{2}.
]

[
\lim_{n\to\infty}\left(1+\frac{1}{n}\right)^n = e.
]

[
A^{-1}=(\det A)^{-1}\operatorname{adj}(A),
\qquad \det(A)\neq 0.
]

[
f(x)=
\begin{cases}
x^2, & x\ge 0,\
-x, & x<0.
\end{cases}
]

[
\nabla\cdot\mathbf{E}=\frac{\rho}{\varepsilon_0},
\qquad
\nabla\times\mathbf{E}=-\frac{\partial\mathbf{B}}{\partial t}.
]

[
\mathbb{E}[X]
=============
\int_{\Omega} X(\omega),d\mathbb{P}(\omega),
]

and a matrix example:

[
Q=
\begin{pmatrix}
1 & 2 & 3\
0 & 1 & 4\
5 & 6 & 0
\end{pmatrix},
\qquad
\det(Q)=1.
]
)";
  std::string result= aofm_normalize_chatgpt_markdown (source);
  QVERIFY (result.find ("\\(e^{i\\pi}+1=0\\)") != std::string::npos);
  QVERIFY (result.find ("\\(a^2+b^2=c^2\\)") != std::string::npos);
  QVERIFY (result.find ("\\(\\int_0^1 x^2,dx=\\frac13\\)") !=
           std::string::npos);
  QVERIFY (result.find ("\\(\\forall x\\in\\mathbb{R},\\ x^2\\ge 0\\)") !=
           std::string::npos);
  QCOMPARE (countOccurrences (result, "\\["), 7);
  QCOMPARE (countOccurrences (result, "\\]"), 7);
  QVERIFY (result.find ("\\[\n\\sum") != std::string::npos);
  QVERIFY (result.find ("\\[\n\\lim") != std::string::npos);
  QVERIFY (result.find ("\\[\nA^{-1}") != std::string::npos);
  QVERIFY (result.find ("\\[\nf(x)=") != std::string::npos);
  QVERIFY (result.find ("\\end{cases}\n\\]") != std::string::npos);
  QVERIFY (result.find ("\\[\n\\nabla") != std::string::npos);
  QVERIFY (result.find ("\\[\n\\mathbb{E}[X]") != std::string::npos);
  QVERIFY (result.find ("\\mathbb{E}[X]\n=\n\\int_{\\Omega}") !=
           std::string::npos);
  QVERIFY (result.find ("=============") == std::string::npos);
  QVERIFY (result.find ("\\[\nQ=\n\\begin{pmatrix}") != std::string::npos);
  QVERIFY (result.find ("x^2, & x\\ge 0,\\\\\n") != std::string::npos);
  QVERIFY (result.find ("1 & 2 & 3\\\\\n") != std::string::npos);
  QVERIFY (result.find ("0 & 1 & 4\\\\\n") != std::string::npos);
}

QTEST_MAIN(TestAofmChatGpt)
#include "aofm_chatgpt_test.moc"
