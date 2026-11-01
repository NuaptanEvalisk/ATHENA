
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-print.scm
;; DESCRIPTION : routines for printing documents
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-print)
  (:use (athena athena tm-files)
        (dynamic fold-edit)
        (utils library cursor)))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Printing wrapper for slides
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (wrapped-print-to-file fname)
  (node-reference-with-export (lambda () (wrapped-print-to-file-ready fname))))

(define (wrapped-print-to-file-ready fname)
  (if (screens-buffer?)
      (let* ((cur (current-buffer))
             (buf (buffer-new)))
        (buffer-copy cur buf)
        (buffer-set-master buf cur)
        (switch-to-buffer buf)
        (dynamic-make-slides)
        (data-art-print-to-file fname)
        (switch-to-buffer cur)
        (buffer-close buf))
      (data-art-print-to-file fname)))

(tm-define (wrapped-print-to-pdf-embeded-with-tm fname)
  (node-reference-with-export
    (lambda () (wrapped-print-to-pdf-embeded-with-tm-ready fname))))


(define (wrapped-print-to-pdf-embeded-with-tm-ready fname)
    (unless (string=? (url-suffix fname) "pdf")
      (texmacs-error "Wrapped-print-to-pdf-embeded-with-tm" "fname is not a pdf"))
    (if (screens-buffer?)
      (let* ((cur (current-buffer))
             (buf (buffer-new)))
        (buffer-copy cur buf)
        (buffer-set-master buf cur)
        (switch-to-buffer buf)
        (dynamic-make-slides)
        (data-art-print-to-file fname)
        (unless (attach-doc-to-exported-pdf fname)
          (notify-now "Fail to attach tm to pdf"))
        (switch-to-buffer cur)
        (buffer-close buf))
      (begin
      (data-art-print-to-file fname)
      (unless (attach-doc-to-exported-pdf fname)
          (notify-now "Fail to attach tm to pdf")))))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Printing commands
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (propose-print-file-name)
  (let ((name (propose-name-buffer))
	(suf "pdf"))
    (if (string-ends? name ".tm")
	(string-append (string-drop-right name 3) "." suf)
	name)))

(tm-property (print-to-file name)
  (:synopsis "Print to file")
  (:argument name print-file "File name")
  (:default  name (propose-print-file-name)))

(tm-property (print-pages first last)
  (:synopsis "Print page selection")
  (:argument  first "First page")
  (:proposals first (list "1" ""))
  (:argument  last "Last page")
  (:proposals last  (list (number->string (get-page-count)) "")))

(tm-property (print-pages-to-file name first last)
  (:synopsis "Print page selection to file")
  (:argument  name print-file "File name")
  (:default   name (propose-print-file-name))
  (:argument  first "First page")
  (:proposals first (list "1" ""))
  (:argument  last "Last page")
  (:proposals last  (list (number->string (get-page-count)) "")))



(tm-define (native-export-pdf-dialog)
  (choose-file wrapped-print-to-file "Save pdf file" "pdf"))
