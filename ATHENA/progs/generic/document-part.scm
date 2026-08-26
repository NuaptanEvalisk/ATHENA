
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

(define (buffer-show-preamble)
  (with t (buffer-tree)
    (when (and (> (tree-arity t) 0)
               (tree-is? (tree-ref t 0) 'hide-preamble))
      (let ((preamble `(show-preamble ,(tree-ref t 0 0)))
            (body `(ignore (document ,@(cdr (tree-children t))))))
        (tree-assign! t `(document ,preamble ,body))))))

(define (buffer-hide-preamble)
  (with t (buffer-tree)
    (when (match? t '(document (show-preamble :%1) (ignore (document :*))))
      (tree-assign! t `(document (hide-preamble ,(tree-ref t 0 0))
				 ,@(tree-children (tree-ref t 1 0)))))))

(tm-define (kbd-remove t forwards?)
  (:require (and (tree-is? t 'show-preamble) (tree-empty? (tree-ref t 0))))
  (buffer-hide-preamble)
  (when (buffer-has-preamble?)
    (tree-remove (buffer-tree) 0 1))
  (update-current-buffer))

(tm-define (in-preamble-mode?)
  (and (buffer-has-preamble?)
       (tree-is? (tree-ref (buffer-tree) 0) 'show-preamble)))

(tm-define (toggle-preamble-mode)
  (:synopsis "Toggle the preamble mode for the document")
  (:check-mark "v" in-preamble-mode?)
  (cond ((in-preamble-mode?)
         (buffer-hide-preamble)
         (when (> (tree-arity (buffer-tree)) 1)
           (tree-go-to (tree-ref (buffer-tree) 1) :start))
         (update-current-buffer))
        ((buffer-has-preamble?)
         (buffer-show-preamble)
         (tree-go-to (buffer-tree) 0 0 :start)
         (update-current-buffer))
        (else (buffer-make-preamble))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Preamble queries
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (document-has-preamble? t)
  (:synopsis "Does the document tree @t contain a preamble?")
  (and (tree? t) (tree-is? t 'document) (> (tree-arity t) 0)
       (tree-in? (tree-ref t 0) '(show-preamble hide-preamble))))

(tm-define (document-get-preamble t)
  (:synopsis "Obtain the preamble of the document tree @t")
  (if (document-has-preamble? t) (tree-ref t 0 0) `(document "")))

(tm-define (buffer-has-preamble?)
  (:synopsis "Does the current buffer contain a preamble?")
  (document-has-preamble? (buffer-tree)))

(tm-define (buffer-get-preamble)
  (:synopsis "Obtain the preamble of the current buffer")
  (document-get-preamble (buffer-tree)))

(tm-define (buffer-make-preamble)
  (:synopsis "Create a preamble for the current document")
  (when (not (buffer-has-preamble?))
    (with t (buffer-tree)
      (tree-insert! t 0 '((hide-preamble (document ""))))
      (buffer-show-preamble)
      (tree-go-to (buffer-tree) 0 0 :start)
      (update-current-buffer))))

(menu-bind preamble-menu
  (if (and (buffer-has-preamble?) (not (in-preamble-mode?)))
      ("Show preamble" (toggle-preamble-mode)))
  (if (not (buffer-has-preamble?))
      ("Create preamble" (toggle-preamble-mode)))
  (if (in-preamble-mode?)
      ("Show main document" (toggle-preamble-mode))))
