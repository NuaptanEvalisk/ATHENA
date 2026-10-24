
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-convert.scm
;; DESCRIPTION : Declaration of data formats and converters
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel athena tm-convert)
  (:use (kernel athena tm-define) (kernel athena tm-modes)))
(import-from (kernel athena tm-preferences))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Finding converters from and to a format
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-public (converters-from . from)
  (native-conversion-reachable from #f))

(define-public (converters-to . to)
  (native-conversion-reachable to #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Actual conversion
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Setting up conversion menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (format<=? fm1 fm2)
  (string<=? (format-get-name fm1) (format-get-name fm2)))

(define-public (converters-from-special* fm suf tm?)
  (let* ((l1 (converters-from fm))
         (l2 (list-filter l1 (lambda (s) (string-ends? s suf))))
         (l3 (map (lambda (s) (string-drop-right s (string-length suf))) l2))
         (l3 (list-filter l3 (lambda (s) (not (format-hidden? s)))))
         (l4 (if tm? l3 (list-filter l3 (lambda (s) (!= s "texmacs"))))))
    (list-sort l4 format<=?)))

(define-public (converters-to-special* fm suf tm?)
  (let* ((l1 (converters-to fm))
         (l2 (list-filter l1 (lambda (s) (string-ends? s suf))))
         (l3 (map (lambda (s) (string-drop-right s (string-length suf))) l2))
         (l3 (list-filter l3 (lambda (s) (not (format-hidden? s)))))
         (l4 (if tm? l3 (list-filter l3 (lambda (s) (!= s "texmacs"))))))
    (list-sort l4 format<=?)))

(define-public (converters-from-special fm suf tm?)
  (converters-from-special* fm suf tm?))

(define-public (converters-to-special fm suf tm?)
  (converters-to-special* fm suf tm?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Other useful subroutines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (tmfile-pair? t)
  (and (tm-compound? t)
       (nnull? (tm-children t))))

(define-public (tmfile-get doc what)
  (and (tm-func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with val (assoc-ref (map tm->list (tm-children doc)) what)
         (if (pair? val) (set! val (car val)))
         val)))

(define-public (tmfile-set doc what val)
  (and (tm-func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with l (reverse (map tm->list (tm-children doc)))
         (set! l (assoc-set! l what (list val)))
         (cons 'document (reverse l)))))

(define-public (tmfile? doc)
  (and (tmfile-get doc 'body) #t))

(define-public (tmfile-extract doc what)
  ;; FIXME: use tmfile-get instead whenever possible
  (if (tree? doc) (set! doc (tree->stree doc)))
  (and (func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with val (assoc-ref (cdr doc) what)
         (if (pair? val) (set! val (car val)))
         (if (tree? val) (set! val (tree->stree val)))
         val)))

(define-public (tmfile-assign doc what val)
  ;; FIXME: use tmfile-set instead whenever possible
  (if (tree? doc) (set! doc (tree->stree doc)))
  (and (func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with l (reverse (cdr doc))
         (set! l (assoc-set! l what (list (tm->tree val))))
         (cons 'document (reverse l)))))

(define (default-init var)
  ;; FIXME: should use C++ code
  (cond ((== var "mode") "text")
        ((== var "language") "english")
        (else "")))

(tm-define (tmfile-init doc var . explicit?)
  (with init (tmfile-extract doc 'initial)
    (if (not init)
        (and (null? explicit?)
             (default-init var))
        (with item (list-find (cdr init) (lambda (x) (== (cadr x) var)))
          (if item (caddr item)
              (and (null? explicit?)
                   (default-init var)))))))

(tm-define (tmfile-style-list doc)
  (with style (tmfile-extract doc 'style)
    (cond ((tm-func? style 'tuple) (cdr style))
          ((string? style) (list style))
          (else (list)))))

(tm-define (tmfile-language doc)
  (let* ((style (tmfile-style-list doc))
         (lans (list-intersection style supported-languages))
         (lan (tmfile-init doc "language" #t)))
    (cond (lan lan)
          ((nnull? lans) (car lans))
          (else "english"))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Utilities for file conversions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-public (file-of-format? u fm)
  (in? (url-suffix u) (cdr (format-get-suffixes* fm))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Viewers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; (define viewer-table (make-ahash-table))

;; (define-public (suffix->viewer suf)
;;   (ahash-ref viewer-table suf))

;; (define-public (save-viewers)
;;   "Save viewers from disk"
;;   (with u "$ATHENA_HOME_PATH/system/viewers.scm"
;;     (save-object u (ahash-table->list viewer-table))))

;; (define (retrieve-viewers)
;;   "Retrieve viewers from disk"
;;   (with u "$ATHENA_HOME_PATH/system/viewers.scm"
;;     (when (url-exists? u)
;;       (set! viewer-table (list->ahash-table (load-object u))))))

;; (retrieve-viewers)
