
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-convert-test.scm
;; DESCRIPTION : Test suite for tm-convert
;; COPYRIGHT   : (C) 2022  Darcy Shen
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(texmacs-module (kernel athena tm-convert-test)
  (:use (kernel athena tm-define)))

(define (regtest-format-from-suffix)
  (regression-test-group
   "format-from-suffix" "string"
   format-from-suffix :none
   (test "texmacs format" "tm" "texmacs")
   (test "texmacs format" "ts" "texmacs")
   (test "texmacs format" "tmml" "tmml")
   (test "texmacs format" "stm" "stm")
   (test "png format" "png" "png")
   (test "no such format" "no-such-format" "generic")))

(tm-define (regtest-tm-convert)
  (let ((n (regtest-format-from-suffix)))
    (display* "Total: " (object->string n) " tests.\n")
    (display "Test suite of tm-convert: ok\n")))
