
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : srfi.scm
;; DESCRIPTION : SRFI syntax used by ATHENA's historical Scheme sources
;; COPYRIGHT   : (C) 1999-2026  Joris van der Hoeven and others
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Guile 3 ships maintained implementations of the forms which TeXmacs once
;; copied into this module for Guile 1.x.  Re-export those implementations so
;; existing ATHENA modules keep their historical import surface without
;; redefining compiler syntax.

(texmacs-module (kernel boot srfi)
  (:use (srfi srfi-2)
        (srfi srfi-8)
        (srfi srfi-16)
        (srfi srfi-26)))

(re-export-syntax and-let* receive case-lambda cut cute)
