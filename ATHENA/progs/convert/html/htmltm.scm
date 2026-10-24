;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : htmltm.scm
;; DESCRIPTION : HTML import entrypoints and LaTeX conversion adapter
;; COPYRIGHT   : (C) 2003 David Allouche, (C) 2026 ATHENA authors
;;
;; This software falls under the GNU general public license version 3 or later.
;; See LICENSE for details.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert html htmltm)
  (:use (convert latex init-latex)))

(define (html-import-latex source)
  ;; Scheme exceptions must not unwind across the native recursive converter.
  (catch #t
    (lambda () (list #t (convert source "latex-snippet" "texmacs-tree")))
    (lambda args (list #f (object->string args)))))

(tm-define (parse-html-snippet s)
  (native-parse-html-import s #t))

(tm-define (parse-html-document s)
  `(!file ,(native-parse-html-import s #f)))

(tm-define (htmltm-as-serial source)
  (tree->stree (native-html-import-convert source html-import-latex)))

(tm-define (html->texmacs-tree source)
  (:synopsis "Convert parsed HTML to a native document tree")
  (let* ((document? (func? source '!file 1))
         (body (native-html-import-convert (if document? (cadr source) source)
                                           html-import-latex))
         (result
           (if document?
               `(document
                  (body ,(if (tree-is? body 'document) body `(document ,body)))
                  (style "generic"))
               body)))
    (tm->tree result)))

(tm-define (html->texmacs source)
  (tree->stree (html->texmacs-tree source)))
