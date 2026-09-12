
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : mathtm-test.scm
;; DESCRIPTION : Test suite for mathtm
;; COPYRIGHT   : (C) 2003  David Allouche
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert mathml mathtm-test)
  (:use (convert html htmltm)))

(tm-define (regtest-mathtm)
  (define (math->tree x) (htmltm-as-serial (cons 'math x)))
  (+
   (regression-test-group
    "mathtm" "mathtm"
    math->tree :none
    (test "identifier" '((mi "x")) "x")
    (test "operator" '((mi "x") (mo "+") (mi "y")) "x+y")
    (test "numeral" '((mn "2") (mo "+") (mi "x")) "2+x"))
   (regression-test-group
    "MathML document import" "mathml-document"
    mathml->tree :none
    (test
     "structured display formula"
     "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><mfrac><msup><mi>x</mi><mn>2</mn></msup><mrow><mi>y</mi><mo>+</mo><mn>1</mn></mrow></mfrac></math>"
     '(document (equation* (frac (concat "x" (rsup "2")) "y+1")))))))
