
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : document-part.scm
;; DESCRIPTION : managing document parts
;; COPYRIGHT   : (C) 2005  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic document-part)
  (:use (generic document-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document preamble
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (kbd-remove t forwards?)
  (:require (and (tree-is? t 'show-preamble) (tree-empty? (tree-ref t 0))))
  (buffer-hide-preamble)
  (when (buffer-has-preamble?)
    (tree-remove (buffer-tree) 0 1))
  (update-current-buffer))

(tm-property (toggle-preamble-mode)
  (:synopsis "Toggle the preamble mode for the document")
  (:check-mark "v" in-preamble-mode?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Preamble queries
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (document-has-preamble? t)
  (:synopsis "Does the document tree @t contain a preamble?"))

(tm-property (document-get-preamble t)
  (:synopsis "Obtain the preamble of the document tree @t"))

(tm-property (buffer-has-preamble?)
  (:synopsis "Does the current buffer contain a preamble?"))

(tm-property (buffer-get-preamble)
  (:synopsis "Obtain the preamble of the current buffer"))

(tm-property (buffer-make-preamble)
  (:synopsis "Create a preamble for the current document"))

(menu-bind preamble-menu
  (if (and (buffer-has-preamble?) (not (in-preamble-mode?)))
      ("Show preamble" (toggle-preamble-mode)))
  (if (not (buffer-has-preamble?))
      ("Create preamble" (toggle-preamble-mode)))
  (if (in-preamble-mode?)
      ("Show main document" (toggle-preamble-mode))))
