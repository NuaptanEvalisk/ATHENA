
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : ref-edit.scm
;; DESCRIPTION : editing routines for references
;; COPYRIGHT   : (C) 2020  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module
 (link ref-edit)
 (:use (utils edit variants) (generic generic-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Finding all standard types of labels/references in a document
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (label-context? t) (tree-in? t (node-group-members "label-tag")))

(tm-define
 (reference-context? t)
 (tree-in? t (node-group-members "reference-tag")))

(tm-define
 (citation-context? t)
 (tree-in? t (node-group-members "citation-tag")))

(tm-define (tie-context? t)
  (or (label-context? t) (reference-context? t) (citation-context? t)))

(define ((named-context? pred? . ids) t)
  (and (pred? t)
       (exists? (lambda (id)
                  (exists? (cut tm-equal? <> id) (tm-children t)))
                ids)))

(tm-define (and-nnull? l)
  (and (nnull? l) l))

(tm-define (search-labels t)
  (tree-search t label-context?))

(tm-define (search-label t . ids)
  (tree-search t (apply named-context? (cons label-context? ids))))

(tm-define (search-references t)
  (tree-search t reference-context?))

(tm-define (search-reference t id)
  (tree-search t (named-context? reference-context? id)))

(tm-define (search-citations t)
  (tree-search t citation-context?))

(tm-define (search-citation t id)
  (tree-search t (named-context? citation-context? id)))

(tm-define (search-tie t id)
  (let* ((id1 (if (string-starts? id "bib-") (string-drop id 4) id))
         (id2 (string-append "bib-" id1)))
    (tree-search t (named-context? tie-context? id1 id2))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Navigation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (list-go-to-first l)
  (tree-go-to (car l) :end))

(tm-define (list-go-to-last l)
  (tree-go-to (cAr l) :end))

(define (list-go-to-previous* l)
  (when (nnull? l)
    (if (path-inf? (tree->path (car l)) (tree->path (cursor-tree)))
        (tree-go-to (car l) :end)
        (list-go-to-previous* (cdr l)))))

(tm-define (list-go-to-previous l)
  (list-go-to-previous* (reverse l)))

(tm-define (list-go-to-next l)
  (when (nnull? l)
    (if (path-inf? (tree->path (cursor-tree)) (tree->path (car l)))
        (tree-go-to (car l) :end)
        (list-go-to-next (cdr l)))))

(tm-define (list-go-to l dir)
  (cond ((nlist? l) (noop))
        ((== dir :first) (list-go-to-first l))
        ((== dir :last) (list-go-to-last l))
        ((== dir :previous) (list-go-to-previous l))
        ((== dir :next) (list-go-to-next l))))

(define current-id '(none))

(tm-define (tie-id)
  (and-with t (tree-innermost tie-context? #t)
    (or (and (exists? (cut tm-equal? <> current-id) (tm-children t))
             current-id)
        (and (tm-atomic? (tm-ref t 0))
             (with key (tm->string (tm-ref t 0))
               (if (string-starts? key "bib-") (string-drop key 4) key))))))

(tm-define (same-ties)
  (and-nnull? (search-tie (buffer-tree) (tie-id))))

(tm-define (go-to-same-tie dir)
  (:applicable (same-ties))
  (set! current-id (tie-id))
  (list-go-to (same-ties) dir))

(tm-define (special-extremal t forwards?)
  (:require (focus-label t))
  (with lab (focus-label t)
    (tree-go-to lab :end)
    (special-extremal lab forwards?)))

(tm-define (special-incremental t forwards?)
  (:require (focus-label t))
  (with lab (focus-label t)
    (tree-go-to lab :end)
    (special-incremental lab forwards?)))

(tm-define (special-extremal t forwards?)
  (:require (tie-context? t))
  (go-to-same-tie (if forwards? :last :first)))

(tm-define (special-incremental t forwards?)
  (:require (tie-context? t))
  (go-to-same-tie (if forwards? :next :previous)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Finding the label key from its number
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (number->labels num)
  (list-filter (find-references num)
               (lambda (x)
                 (not (or (string-starts? x "auto-")
                          (string-starts? x "bib-"))))))

(define (abbr->type s)
  (cond ((in? s '("t" "th" "thm")) "theorem")
        ((in? s '("p" "pr" "prop")) "proposition")
        ((in? s '("l" "le" "lm" "lem")) "lemma")
        ((in? s '("co" "cor" "corr")) "corollary")
        ((in? s '("def" "dfn" "defn")) "definition")
        ((in? s '("not")) "notation")
        ((in? s '("ax")) "axiom")
        ((in? s '("conv")) "convention")
        ((in? s '("conj")) "conjecture")
        ((in? s '("rem")) "remark")
        ((in? s '("war")) "warning")
        ((in? s '("ex")) "example")
        ((in? s '("exc" "exe" "exer")) "exercise")
        ((in? s '("prb" "prob")) "problem")
        ((in? s '("sol")) "solution")
        ((in? s '("c" "ch" "chap")) "chapter")
        ((in? s '("s" "sec")) "section")
        ((in? s '("ss" "ssec" "subs" "subsec")) "subsection")
        ((in? s '("par" "para")) "paragraph")
        ((in? s '("e" "eq" "eqn" "equa")) "equation")
        ((in? s '("fig")) "figure")
        ((in? s '("tab")) "table")
        ((in? s '("alg" "algo")) "algorithm")
        (else s)))

(define (type->types type)
  (cond ((== type "section") (list "section" "subsection" "subsubsection"))
        ((== type "subsection") (list "subsection" "subsubsection"))
        ((== type "paragraph") (list "paragraph subparagraph"))
        ((== type "figure") (list "big-figure" "small-figure"))
        ((== type "table") (list "big-table" "small-table"))
        ((== type "algorithm") (list "algorithm"
                                     "specified-algorithm"
                                     "named-algorithm"
                                     "named-specified-algorithm"))
        ((== type "equation") (list "equation" "eqnarray" "eqnarray*"))
        (else (list type))))

(define (abbr->types s)
  (type->types (abbr->type s)))

(define (previous-word t)
  (cond ((not (and (tree? t) (tree-up t))) #f)
        ((tree-is? (tree-up t) 'concat)
         (and-let* ((p (tree-up t))
                    (i (- (tree-index t) 1)))
           (while (and (>= i 0) (not (tree-atomic? (tree-ref p i))))
             (set! i (- i 1)))
           (and (>= i 0)
                (let* ((s (tm-string-trim-right (tree->string (tree-ref p i))))
                       (j (string-search-backwards " " (string-length s) s)))
                  (if (>= j 0) (substring s (+ j 1) (string-length s)) s)))))
        ((or (tree-atomic? t) (reference-context? t) (tree-is? t 'inactive))
         (previous-word (tree-up t)))
        (else #f)))

(define type-list
  (with l
        (append
         (node-group-members "enunciation-tag")
         (node-group-members "section-tag")
         (node-group-members "equation-tag")
         (node-group-members "algorithm-tag"))
        (append (map symbol->string l) (list "figure" "table"))))

(define (plural s)
  (cond ((== s "") s)
        ((string-ends? s "y")
         (string-append (substring s 0 (- (string-length s) 1)) "ies"))
        (else
         (string-append s "s"))))

(define (word-matches? w t)
  (or (== w t)
      (== w (plural t))
      (== w (translate-from-to t "english" (get-init "language")))
      (== w (translate-from-to (plural t) "english" (get-init "language")))))

(define (word->type s*)
  (with s (locase-all s*)
    (with f (list-find type-list (cut word-matches? s <>))
      (or f (cond ((== s "(") "equation")
                  (else s))))))
    
(define (word->types s)
  ;;(display* "word->types " s "\n")
  (type->types (word->type s)))

(define (label-matches? t types)
  ;;(display* "label-matches? " (tree->path t) ", " types "\n")
  (and (tree? t)
       (or (tree-in? t (map string->symbol types))
           (and (tree-is? t 'concat)
                (or (in? "subsubsection" types) (in? "subparagraph" types))
                (exists? (cut label-matches? <> types)
                         (tree-children t))))))

(define (matching-labels ids types)
  ;;(display* "matching-labels " ids ", " types "\n")
  (with labs (apply search-label (cons (buffer-tree) ids))
    (if (null? labs) (list)
        (let* ((pred? (cut label-matches? <> types))
               (test? (lambda (lab) (tree-search-upwards lab pred?)))
               (hits (list-filter labs test?))
               (text (lambda (lab) (tm->string (tm-ref lab 0)))))
          (list-filter (map text hits) identity)))))

(tm-define (number->label t)
  ;;(display* "number->label " t "\n")
  (and-let* ((s (tm->string t))
             (types (or (with i (string-search-forwards ":" 0 s)
                          (and (>= i 0) (abbr->types (substring s 0 i))))
                        (and-with w (previous-word t)
                          (word->types w))
                        (list)))
             (num (with i (string-search-forwards ":" 0 s)
                    (if (>= i 0) (substring s (+ i 1) (string-length s)) s)))
             (labs (and-nnull? (number->labels num))))
    (if (null? (cdr labs)) (car labs)
        (with f (matching-labels labs types)
          (if (null? f) (car labs) (car f))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Activating references whose keys are inferred
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (in-inactive-reference?)
  (and (in-preview-ref?)
       (tree-atomic? (cursor-tree))
       (reference-context? (tree-up (cursor-tree)))
       (tree-is? (tree-up (tree-up (cursor-tree))) 'inactive)))

(define (label-exists? s)
  (with t (set-of-labels (buffer-tree))
    (ahash-ref t s)))

(tm-define (kbd-return)
  (:require (in-inactive-reference?))
  (with-innermost t reference-context?
    (for (c (tree-children t))
      (and-with s (tm->string c)
        (when (not (label-exists? s))
          (and-with id (number->label c)
            (tree-assign c id)))))
    (former)))
