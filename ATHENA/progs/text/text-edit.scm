
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : text-edit.scm
;; DESCRIPTION : editing routines for text mode
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module
 (text text-edit)
 (:use (utils library tree)
       (utils edit variants)
       (utils edit selections)
       (generic generic-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Inserting document, author and abstract data
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define doc-data-inactive-tags (node-group-members "doc-title-inactive-tag"))

(tm-define (kbd-space-bar t shift?)
  (:require (and (tree-is-buffer? t) (in-text?)
		 (!= (get-env "language") "verbatim")))
  (let* ((b (before-cursor))
	 (p (get-preference "text spacebar")))
    (cond ((== p "allow multiple spaces")
	   (insert " "))
	  ((and (== b " ") (== p "no multiple spaces"))
	   (noop))
	  ((== b " ")
	   (remove-text #f)
	   (make-space "1em"))
	  ((and (tree? b) (tree-func? b 'space 1))
	   (if (and (tree-atomic? (tree-ref b 0))
		    (string-ends? (tree->string (tree-ref b 0)) "em"))
	       (make-space "1em")
	       (geometry-horizontal b #t)))
	  (else (insert " ")))))

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'title))
  (go-end-line)
  (insert-return))

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'doc-title))
  (make-doc-data-element 'doc-author))

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'author-name))
  (make-author-data-element 'author-affiliation))

(tm-define (kbd-enter t shift?)
  (:require (or
              (tree-is? t 'abstract-arxiv)
              (tree-is? t 'abstract-pacs)
              (tree-is? t 'abstract-acm)
              (tree-is? t 'abstract-msc)
              (tree-is? t 'abstract-keywords)))
  (with t (tree-search-upwards
            t '(abstract-msc abstract-acm abstract-pacs
                             abstract-arxiv abstract-keywords))
    (with pos (1+ (tree-down-index t))
      (tree-insert! t pos `((concat "")))
      (tree-go-to t pos 0 0))))

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'doc-inactive))
  (doc-data-activate-here))

(tm-define (doc-data-clean t)
  (for (c (reverse (tree-children t)))
    (cond ((tree-empty? c)
           (tree-remove t (tree-index c) 1))
          ((and (tree-func? c 'doc-author 1)
                (tree-empty? (tree-ref c 0)))
           (tree-remove t (tree-index c) 1))
          ((and (tree-func? c 'doc-author 1)
                (tm-is? (tree-ref c 0) 'author-data))
           (doc-data-clean (tree-ref c 0))))))

(tm-define (kbd-remove t forwards?)
  (:require (tree-search-upwards t 'doc-data))
  (with d (tree-search-upwards t 'doc-data)
    (former t forwards?)
    (when (and (tree->path d) (tree-is? d 'doc-data))
      (doc-data-clean d))))

(tm-define (set-doc-title-options opts)
  (with-innermost t 'doc-data
    (with opts-trees (select t '(doc-title-options))
      (if (null? opts)
          (when (nnull? opts-trees)
            (with old (car opts-trees)
              (tree-remove (tree-up old) (tree-index old) 1)))
          (begin
            (when (null? opts-trees)
              (make-doc-data-element 'doc-title-options)
              (set! opts-trees (select t '(doc-title-options))))
            (tree-set (car opts-trees) `(doc-title-options ,@opts)))))))

(tm-define (get-doc-title-options)
  (with-innermost t 'doc-data
    (with opts-trees (select t '(doc-title-options :%1))
      (map tree->stree opts-trees))))

(tm-define (test-doc-title-clustering? mode)
  (with cl (list "cluster-all" "cluster-by-affiliation")
    (with old (get-doc-title-options)
      (if mode (in? mode old) (null? (list-intersection cl old))))))

(tm-define (set-doc-title-clustering mode)
  (:check-mark "*" test-doc-title-clustering?)
  (with cl (list "cluster-all" "cluster-by-affiliation")
    (with old (list-difference (get-doc-title-options) cl)
      (set-doc-title-options (if mode (cons mode old) old)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Activation and disactivation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (doc-data-go-to-active t i)
  (cond
    ((< i 0) (tree-go-to t :end))
    ((tree-in? t i (node-group-members "doc-title-inactive-tag"))
     (doc-data-go-to-active t (- i 1)))
    ((not (cursor-inside? (tree-ref t i))) (tree-go-to t i :end))))

(tm-define (doc-data-activate-here)
  (with-innermost dd 'doc-data
    (with-innermost t 'doc-inactive
      (tree-remove-node! t 0)
      (doc-data-go-to-active dd (tree-down-index dd)))))

(tm-define
 (doc-data-has-hidden?)
 (with-innermost
  t
  'doc-data
  (with l
        (cdr (tree->list t))
        (with fun
              (lambda (t)
                (or (tree-in? t (node-group-members "doc-title-inactive-tag"))
                    (tree-is? t 'doc-inactive)))
              (list-or (map fun l))))))

(tm-define (doc-data-deactivated?)
  (with-innermost t 'doc-data
    (with l (cdr (tree->list t))
      (list-or (map (lambda (t) (== (tm-car t) 'doc-inactive)) l)))))

(define (doc-data-activate-one t)
  (when (tree-is? t 'doc-inactive)
    (tree-remove-node! t 0)))

(tm-define (doc-data-activate-all)
  (with-innermost t 'doc-data
    (with i (tree-down-index t)
      (with l (cdr (tree->list t))
	(for-each doc-data-activate-one l))
      (doc-data-go-to-active t i))))

(define (doc-data-deactivate-one t)
  (if (in? (tm-car t) doc-data-inactive-tags)
      (tree-insert-node! t 0 '(doc-inactive))))

(tm-define (doc-data-deactivate-all)
  (with-innermost t 'doc-data
    (with l (cdr (tree->list t))
      (for-each doc-data-deactivate-one l))))

(tm-define (doc-data-activate-toggle)
  (if (doc-data-deactivated?)
      (doc-data-activate-all)
      (doc-data-deactivate-all)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Making letter headings or titles
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Sectional commands
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define
 (section-context? t)
 (tree-in? t (numbered-unnumbered-append (node-group-members "section-tag"))))

(tm-define (previous-section)
  (with bt (buffer-tree)
    (and (cursor-inside? bt)
	 (with bp (list-drop (cursor-path) (length (tree->path bt)))
	   (with sp (path-previous-section bt bp)
	     (and (!= sp bp) (path->tree (append (tree->path bt) sp))))))))

(tm-define
 (go-to-section-title)
 (and-with
  s
  (previous-section)
  (when (or (node-group-contains? "section-tag" (tm-car s))
            (node-group-contains? "section*-tag" (tm-car s)))
        (tree-go-to s 0 :start))))

(tm-define (kbd-enter t shift?)
  (:require (section-context? t))
  (tree-go-to t :end)
  (insert-return))

(tm-define (label-insert t)
  (:require (section-context? t))
  (tree-go-to t :end)
  (make 'label))

(tm-define (focus-label t)
  (:require (section-context? t))
  (and-with p (tree-up t)
    (and (tm-func? p 'concat)
         (focus-search-label p))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Routines for lists, enumerations and descriptions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (list-context? t) (tree-in? t (node-group-members "list-tag")))

(tm-define
 (itemize-context? t)
 (tree-in? t (node-group-members "itemize-tag")))

(tm-define
 (enumerate-context? t)
 (tree-in? t (node-group-members "enumerate-tag")))

(tm-define (todo-list-context? t) (tree-in? t (node-group-members "todo-tag")))

(tm-define
 (itemize-enumerate-context? t)
 (or (tree-in? t (node-group-members "itemize-tag"))
     (tree-in? t (node-group-members "enumerate-tag"))))

(tm-define (todo-list-item-context? t)
  (or (tree-is? t 'todo-item)
      (tree-is? t 'done-item)
      (and (tree-is? t 'item)
           (tree-innermost 'todo-list #t))))

(tm-define (cloud-todo-list-item-context? t)
  (or (tree-is? t 'cloud-todo-item)
      (tree-is? t 'cloud-done-item)
      (and (tree-innermost 'cloud-todo-list #t)
           (or (tree-innermost 'cloud-todo-item #t)
               (tree-innermost 'cloud-done-item #t)
               (tree-is? t 'item)))))

(define (todo-current-item)
  (or (tree-innermost 'todo-item #t)
      (tree-innermost 'done-item #t)
      (and (tree-innermost 'todo-list #t)
           (tree-innermost 'item #t))))

(define (cloud-todo-current-marker)
  (or (tree-innermost 'cloud-todo-item #t)
      (tree-innermost 'cloud-done-item #t)
      (and (tree-innermost 'cloud-todo-list #t)
           (tree-innermost 'item #t))))

(define (cloud-todo-current-row)
  (and-with marker (cloud-todo-current-marker)
    (or (and-with p (tree-up marker)
          (and (tree? p) p))
        marker)))

(tm-define
 (make-tmlist l)
 (with flag?
       (and (selection-active-non-small?)
            (in? l (node-group-members "description-tag")))
       (wrap-selection-any
        (make l)
        (if flag? (insert '(item* "")) (make-item)))))

(tm-define
 (make-item)
 (if (not (make-return-after))
     (with lab
           (inside-which (node-group-members "list-tag"))
           (cond
             ((in? lab (node-group-members "itemize-tag")) (make 'item))
             ((in? lab (node-group-members "enumerate-tag")) (make 'item))
             ((in? lab (node-group-members "todo-tag")) (make 'item))
             ((in? lab (node-group-members "description-tag")) (make 'item*))
             (else (make 'item))))))

(tm-define (kbd-enter t shift?)
  (:require (list-context? t))
  (if shift? (make-return-after) (make-item)))

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'item*))
  (go-end-of 'item*))

(tm-define (todo-toggle-current)
  (let ((t (todo-current-item)))
    (when t
      (tree-assign-node! t (if (tree-is? t 'done-item)
                               'todo-item
                               'done-item)))))

(tm-define (cloud-todo-toggle-current)
  (let ((t (cloud-todo-current-marker)))
    (when t
      (let* ((done? (tree-is? t 'cloud-todo-item))
             (row (cloud-todo-current-row)))
        (tree-assign-node! t (if done?
                                 'cloud-done-item
                                 'cloud-todo-item))
        (when (and row (defined? 'google-cloud-todo-push-item))
          (google-cloud-todo-push-item row done?))))))

(tm-define (mouse-toggle-todo-item t)
  (:type (-> tree void))
  (:synopsis "Toggle a todo-list item using the mouse")
  (:secure #t)
  (when (and (tree? t) (tree->path t))
    (tree-go-to t :start))
  (todo-toggle-current))

(tm-define (mouse-toggle-cloud-todo-item t)
  (:type (-> tree void))
  (:synopsis "Toggle a cloud todo-list item using the mouse")
  (:secure #t)
  (when (and (tree? t) (tree->path t))
    (tree-go-to t :start))
  (cloud-todo-toggle-current))

(tm-define (focus-label t)
  (:require (or (list-context? t) (tree-is? t 'bib-list)))
  (and-with doc (tree-down t)
    (and (tree-is? doc 'document)
         (and-with par (tree-down doc)
           (focus-search-label par)))))

(tm-define (numbered-context? t)
  (:require (or (itemize-context? t) (enumerate-context? t)))
  #t)

(tm-define (numbered-numbered? t)
  (:require (enumerate-context? t))
  #t)

(tm-define (numbered-toggle t)
  (:require (itemize-context? t))
  (variant-set t 'enumerate))

(tm-define (numbered-toggle t)
  (:require (enumerate-context? t))
  (variant-set t 'itemize))

(tm-define (standard-parameters l)
  (:require (== l "itemize"))
  (list-remove (cons* "itemize-levels"
                      "item-tag" "item-1" "item-2" "item-3" "item-4"
                      (search-parameters "itemize-1"))
               "itemize-level"))

(ahash-set! inhibit-global-table "item-tag" #t)
(ahash-set! inhibit-local-table "item-1" #t)
(ahash-set! inhibit-local-table "item-2" #t)
(ahash-set! inhibit-local-table "item-3" #t)
(ahash-set! inhibit-local-table "item-4" #t)

(tm-define (standard-parameters l)
  (:require (== l "enumerate"))
  (list-remove (cons* "enumerate-levels"
                      "enum-tag" "enum-1" "enum-2" "enum-3" "enum-4"
                      (search-parameters "enumerate-1"))
               "enumerate-level"))

(ahash-set! inhibit-global-table "enum-tag" #t)
(ahash-set! inhibit-local-table "enum-1" #t)
(ahash-set! inhibit-local-table "enum-2" #t)
(ahash-set! inhibit-local-table "enum-3" #t)
(ahash-set! inhibit-local-table "enum-4" #t)

(tm-define (parameter-choice-list l)
  (:require (in? l (list "itemize-levels" "enumerate-levels")))
  (list "1" "2" "3" "4"))

(tm-define (parameter-choice-list l)
  (:require (in? l (list "item-tag" "item-1" "item-2" "item-3" "item-4")))
  (list "<bullet>" "<circ>" "<minus>" "<cdot>" "*"
        "<rightarrow>" "<Rightarrow>"
        "<triangleright>" "<blacktriangleright>" :other))

(tm-define (parameter-choice-list l)
  (:require (in? l (list "enum-tag" "enum-1" "enum-2" "enum-3" "enum-4")))
  (list (list "1, 2, 3, ..." '(macro "x" (arg "x")))
        (list "a, b, c, ..." '(macro "x" (number (arg "x") "alpha")))
        (list "A, B, C, ..." '(macro "x" (number (arg "x") "Alpha")))
        (list "i, ii, iii, ..." '(macro "x" (number (arg "x") "roman")))
        (list "I, II, III, ..." '(macro "x" (number (arg "x") "Roman")))
        :other))
