
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-html.scm
;; DESCRIPTION : setup html converters
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert html init-html))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Html
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (html-recognizes-at? s pos)
  (set! pos (format-skip-spaces s pos))
  (cond ((format-test? s pos "<html") #t)
	((format-test? s pos "<xhtml") #t)
	((format-test? s pos "<body") #t)
	((format-test? s pos "<title") #t)
	((format-test? s pos "<!doctype html") #t)
	((format-test? s pos "<math") #t)
	((format-test? s pos "<?xml ")
	 (html-recognizes-at? s (format-skip-line s pos)))
	((format-test? s pos "<!doctype ")
	 (html-recognizes-at? s (format-skip-line s pos)))
	(else #f)))

(define (html-recognizes? s)
  (and (string? s) (html-recognizes-at? s 0)))

(define-format html
  (:name "HTML")
  (:suffix "html" "htm")
  (:recognize html-recognizes?))

(lazy-define (convert html htmltm) parse-html-snippet)
(lazy-define (convert html htmltm) parse-html-document)
(lazy-define (convert html htmltm) html->texmacs)
(lazy-define (convert html htmltm) html->texmacs-tree)
(lazy-define (convert html htmlout) serialize-html)
(lazy-define (convert html html-export) texmacs->html)

(converter html-document html-stree
  (:function parse-html-document))

(converter html-stree html-document
  (:function serialize-html))

(converter html-snippet html-stree
  (:function parse-html-snippet))

(converter html-stree html-snippet
  (:function serialize-html))

(converter html-stree texmacs-stree
  (:function html->texmacs))

(converter html-stree texmacs-tree
  (:function html->texmacs-tree))

(converter texmacs-stree html-stree
  (:function-with-options texmacs->html)
  (:option "texmacs->html:css-stylesheet" "---"))

;; Keep typed metadata for selections as well as complete documents. Option
;; defaults remain owned by the existing stree edge.
(tm-define (texmacs-tree->html-stree x opts)
  (texmacs->html x
    (append opts (std-converter-options "texmacs-stree" "html-stree"))))

(converter texmacs-tree html-stree
  (:function-with-options texmacs-tree->html-stree))

(tm-define (texmacs->html-document x opts)
  (serialize-html (texmacs->html x opts)))

;; Direct native source route: converting via texmacs-stree first drops the
;; typed MATH_BLOCK/MATH_GRID/MATH_SCRIPTS metadata. Keeping the native tree
;; until this entrypoint lets the standard export-file conversion graph use
;; the same lossless path as the explicit HTML exporter. No v2 fallback or
;; alternate representation is introduced inside the XML v3 reader.
(converter texmacs-tree html-document
  (:function-with-options texmacs->html-document)
  (:option "texmacs->html:css-stylesheet" "---"))
