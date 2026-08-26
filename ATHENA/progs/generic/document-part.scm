
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
;; Flatten old-style projects into one file
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (inclusion-children t)
  (cond ((tree-is? t 'with) (inclusion-children (cAr (tree-children t))))
	((tree-is? t 'document) (tree-children t))
	(else (list t))))

(define (expand-includes-one t r)
  (if (tree-is? t 'include)
      (with u (url-relative r (unix->url (tree->string (tree-ref t 0))))
	(inclusion-children (tree-load-inclusion u)))
      (list (expand-includes t r))))

(define (expand-includes t r)
  (cond ((tree-atomic? t) t)
	((tree-is? t 'document)
	 (with l (map (lambda (x) (expand-includes-one x r)) (tree-children t))
	   (cons 'document (apply append l))))
	(else
	 (with l (map (lambda (x) (expand-includes x r)) (tree-children t))
	   (cons (tree-label t) l)))))

(tm-define (buffer-expand-includes)
  (with t (buffer-tree)
    (tree-assign! t (expand-includes (buffer-tree) (buffer-master)))))

(define (buffer-master?) (== (get-init "project-flag") "true"))
(tm-define (buffer-toggle-master)
  (:synopsis "Toggle using current buffer as master file of project")
  (:check-mark "v" buffer-master?)
  (init-env "project-flag"
            (if (== (get-init "project-flag") "true") "false" "true")))

(define (project-attach* u)
  (with name (url->unix (url-delta (current-buffer) u))
    (project-attach name)))

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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Buffer with included files
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (tm-include? t)
  (and (tm-func? t 'include 1)
       (tm-atomic? (tm-ref t 0))))

(tm-define (tm-get-includes doc)
  (cond ((tm-func? doc 'with)
	 (tm-get-includes (tm-ref doc :last)))
	((tm-func? doc 'document)
	 (append-map tm-get-includes (tm-children doc)))
	((tm-include? doc)
	 (list (tm->string (tm-ref doc 0))))
	(else (list))))

(tm-define (buffer-get-includes)
  (tm-get-includes (buffer-tree)))

(tm-define (buffer-contains-includes?)
  (nnull? (buffer-get-includes)))

(menu-bind preamble-menu
  (if (and (buffer-has-preamble?) (not (in-preamble-mode?)))
      ("Show preamble" (toggle-preamble-mode)))
  (if (not (buffer-has-preamble?))
      ("Create preamble" (toggle-preamble-mode)))
  (if (in-preamble-mode?)
      ("Show main document" (toggle-preamble-mode))))

(menu-bind project-manage-menu
  (if (!= (url-suffix (current-buffer)) "tp")
      ("Use as master" (buffer-toggle-master)))
  (when (buffer-contains-includes?)
    ("Expand inclusions" (buffer-expand-includes)))
  ---
  (when (not (project-attached?))
    ("Attach master"
     (choose-file project-attach* "Attach master file for project" "texmacs")))
  (when (project-attached?)
    ("Detach master" (project-detach))))
