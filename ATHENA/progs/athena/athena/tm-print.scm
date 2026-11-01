
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

(tm-define (wrapped-import-pdf-embeded-with-tm tem-pdf)
  (let* ((tem-dir (url-temp-dir))
         (tem-tm (url-append tem-dir "tem.tm"))
         (tem-tm2 (url-append tem-dir "extracted.tm")))
    (if (extract-attachments tem-pdf)
        (begin
          (string-save
            (serialize-texmacs
              (pdf-replace-linked-path
                (tree-import
                  (url-relative tem-tm (pdf-get-attached-main-tm tem-pdf))
                  "texmacs")
                tem-pdf))
            tem-tm2)
          (load-buffer tem-tm2))
        (begin
          (notify-now "Can not extract attachments from PDF")
          (texmacs-error "pdf" "Can not extract attachments from PDF")))))

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

(tm-define (attach-doc-to-exported-pdf fname)
  (let* ((tem-url (buffer-new))
         (new-url (url-relative tem-url (string-append (url-basename fname) ".tm")))
         (cur-url (current-buffer-url))
         (cur-tree (buffer-get cur-url))
         (linked-file (pdf-get-linked-file-paths cur-tree cur-url))
         (linked-file-with-main (array-url-append new-url linked-file))
         (new-tree (pdf-replace-linked-path cur-tree cur-url)))
    (buffer-rename tem-url new-url)
    (buffer-copy cur-url new-url)
    ;; copy also attachments and auxiliary data
    (with-buffer cur-url
      (let* ((attl (list-attachments)) 
             (atts (map get-attachment attl))
             (auxl (list-auxiliaries))
             (auxs (map get-auxiliary auxl)))
        (with-buffer new-url
          (for-each set-attachment attl atts)
          (for-each set-auxiliary auxl auxs))))
    (buffer-save new-url)
    (pdf-make-attachments fname linked-file-with-main fname)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Printing commands
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (propose-print-file-name)
  (let ((name (propose-name-buffer))
	(suf (printer-file-suffix)))
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


(tm-define (choose-file-and-print-page-selection start end)
  (:argument  start "First page")
  (:proposals start (list "1" ""))
  (:argument  end "Last page")
  (:proposals end (list (number->string (get-page-count)) ""))
  (choose-file (lambda (name)
                 (node-reference-with-export (lambda () (print-pages-to-file name start end))))
	       "Print page selection to file" (printer-file-format)
	       "Print:"))

(tm-property (prepared-print-pages first last)
  (:argument first "First page")
  (:proposals first (list "1" ""))
  (:argument last "Last page")
  (:proposals last (list (number->string (get-page-count)) "")))

;; Finite business adapters for the native application-shell toolbar.  File
;; and page-selection UI remains here until those dialogs are migrated, but the
;; toolbar never expands or executes legacy Scheme menu ASTs.
(tm-define (native-print-to-file-dialog)
  (choose-file prepared-print-to-file "Print all to file"
               (printer-file-format) "Print:"))

(tm-define (native-print-page-selection-dialog)
  (interactive prepared-print-pages))

(tm-define (native-print-page-selection-to-file-dialog)
  (interactive choose-file-and-print-page-selection))

(tm-define (native-export-pdf-dialog)
  (choose-file wrapped-print-to-file "Save pdf file" "pdf"))
