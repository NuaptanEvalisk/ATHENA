
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : link-navigate.scm
;; DESCRIPTION : navigation routines for links
;; COPYRIGHT   : (C) 2006  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (link link-navigate)
  (:use (utils library cursor)
        (generic generic-edit)))
(import-from (kernel athena tm-preferences))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Actual navigation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (go-to-id id . opt-from)
  (:synopsis "Jump to the first locus with a given identifier @id")
  (:argument opt-from "Optional path for the cursor history")
  (with l (id->trees id)
    (if (nnull? l)
        (begin
          (if (nnull? opt-from) (cursor-history-add (car opt-from)))
          (tree-go-to (car l) :end)
          (if (nnull? opt-from) (cursor-history-add (cursor-path))))
        (and (resolve-id id)
             (delayed (:idle 25) (apply go-to-id (cons id opt-from)))))))
