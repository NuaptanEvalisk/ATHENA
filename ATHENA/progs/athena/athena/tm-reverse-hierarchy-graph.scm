
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-reverse-hierarchy-graph.scm
;; DESCRIPTION : Reverse hierarchy graph commands
;; COPYRIGHT   : (C) 2026  Felix
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-reverse-hierarchy-graph)
  (:use (kernel athena tm-secure)))

(define-secure-symbols reverse-hierarchy-graph-render
                       global-hierarchy-graph-show)

(tm-define (open-reverse-hierarchy-graph)
  (:synopsis "Open the reverse namespace hierarchy graph for the current file")
  (reverse-hierarchy-graph-show))

(tm-define (insert-reverse-hierarchy-graph)
  (:synopsis "Insert the reverse namespace hierarchy graph for the current file")
  (reverse-hierarchy-graph-insert))

(tm-define (open-direct-hierarchy-graph)
  (:synopsis "Open the direct namespace hierarchy graph for the current namespace")
  (direct-hierarchy-graph-show))

(tm-define (open-global-hierarchy-graph)
  (:synopsis "Open the direct namespace hierarchy graph for the vault root namespace")
  (global-hierarchy-graph-show))
