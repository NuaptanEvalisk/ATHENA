
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : link-edit.scm
;; DESCRIPTION : editing routines for links
;; COPYRIGHT   : (C) 2006  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (link link-edit)
  (:use (link locus-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Utility routines for links
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (list->assoc-list l)
  (if (or (null? l) (null? (cdr l))) '()
      (cons (cons (car l) (cadr l)) (list->assoc-list (cddr l)))))

(define (link-flatten-sub t)
  (cond ((tm-func? t 'script)
	 (cons* 'script (tree->stree (tree-ref t 0))
		(cdr (tree-children t))))
	((tm-func? t 'attr)
	 (list->assoc-list (cdr (tree->stree t))))
	(else (tree->stree t))))

(tm-define (link-flatten ln)
  (cons (tm-car ln) (map link-flatten-sub (tm-cdr ln))))

(tm-define (link-type ln)
  (if (tree? ln) (set! ln (link-flatten ln)))
  (and (func? ln 'link) (cadr ln)))

(tm-define (link-attributes ln)
  (if (tree? ln) (set! ln (link-flatten ln)))
  (and (func? ln 'link) (caddr ln)))

(tm-define (link-vertices ln)
  (if (tree? ln) (set! ln (link-flatten ln)))
  (and (func? ln 'link) (cdddr ln)))

(tm-define (link-source ln)
  (car (link-vertices ln)))

(tm-define (link-target ln)
  (cadr (link-vertices ln)))

(tm-define (vertex->id r)
  (and (func? r 'id 1) (string? (cadr r)) (cadr r)))

(tm-define (vertex->url r)
  (and (func? r 'url 1) (string? (cadr r)) (cadr r)))

(tm-define (vertex->script r)
  (and (func? r 'script) (cadr r)))
