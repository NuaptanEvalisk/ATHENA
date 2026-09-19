
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : document-edit.scm
;; DESCRIPTION : setting global document properties
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic document-edit)
  (:use (utils base environment)
	(athena athena tm-tools)
        (utils library length)
        (utils library cursor)
        (generic generic-edit)
        (generic document-style)))
(import-from (kernel athena tm-preferences))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Preamble mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (toggle-source-mode)
  (:synopsis "Toggle source code editing mode")
  (:check-mark "v" in-source-mode?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Global environment variables
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (init-env var val)
  (:check-mark "*" test-init?))

(tm-define (init-interactive-env var)
  (:interactive #t)
  (interactive (lambda (s) (set-init-env var s))
    (list (or (logic-ref env-var-description% var) var) "string"
          (get-init-env var))))

(tm-define (toggle-init-env var)
  (:check-mark "*" test-init-true?)
  (with new (if (== (get-init-env var) "true") "false" "true")
    (init-default var)
    (delayed
      (when (!= new (get-init-env var))
        (set-init-env var new)))))

(tm-property (init-font val . opts)
  (:check-mark "*" test-init-font?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Initial environment management in specific buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (initial-set-tree u var val)
  (when (tm? val)
    (with-buffer u
      (init-env-tree var val))))

(tm-define (initial-set u var val)
  (when (string? val)
    (with-buffer u
      (init-env var val))))

(tm-define (initial-get-tree u var)
  (or (with-buffer u
        (get-init-tree var))
      (tree "")))

(tm-define (initial-get u var)
  (or (with-buffer u
        (get-init-env var))
      ""))

(tm-define (initial-defined? u var)
  (with-buffer u
    (style-has? var)))

(tm-define (initial-has? u var)
  (with-buffer u
    (init-has? var)))

(tm-define (initial-default u . vars)
  (with-buffer u
    (apply init-default vars)))


(tm-define (buffer-get-metadata u kind)
  (or (with-buffer u
        (get-metadata kind))
      ""))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Text and paragraph properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (set-default-document-language)
  (:check-mark "*" test-default-document-language?))

(tm-property (set-document-language lan)
  (:check-mark "*" test-document-language?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Main page layout
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (init-default-page-medium)
  (:check-mark "*" test-default-page-medium?))

(tm-property (init-page-medium s)
  (:check-mark "*" test-page-medium?))

(tm-property (default-page-type)
  (:check-mark "*" test-default-page-type?))

(tm-property (init-page-type s)
  (:check-mark "*" test-page-type?))

(tm-property (init-page-size w h)
  (:argument w "Page width")
  (:argument h "Page height"))

(tm-property (init-default-page-orientation)
  (:check-mark "*" test-default-page-orientation?))

(tm-property (init-page-orientation s)
  (:check-mark "*" test-page-orientation?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Wrapper for global page rendering
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (init-default-page-rendering)
  (:check-mark "*" test-default-page-rendering?))

(tm-define (init-page-rendering s)
  (:check-mark "*" test-page-rendering?)
  (document-apply-page-rendering-state s)
  (cond ((== s "panorama")
         (delayed (:idle 25) (fit-all-to-screen)))
        ((== s "slideshow")
         (delayed (:idle 25) (restore-zoom "slideshow")))
        (else
         (delayed (:idle 25) (restore-zoom s)))))

(tm-define (initial-get-page-rendering u)
  (with-buffer u
    (get-init-page-rendering)))

(tm-define (initial-set-page-rendering u s)
  (with-buffer u
    (init-page-rendering s)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Further page layout settings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (toggle-visible-header-and-footer)
  (:synopsis "Toggle visibility of headers and footers in 'page' paper mode")
  (:check-mark "v" visible-header-and-footer?))

(tm-property (toggle-page-width-margin)
  (:synopsis "Toggle mode for determining margins from paragraph width")
  (:check-mark "v" page-width-margin?))

(tm-property (toggle-page-screen-margin)
  (:synopsis "Toggle mode for using special margins for screen editing")
  (:check-mark "v" not-page-screen-margin?))

(tm-property (toggle-reduced-margins)
  (:synopsis "Toggle mode for using reduced margins to save paper")
  (:check-mark "v" reduced-margins?))

(tm-property (toggle-indent-paragraphs)
  (:synopsis "Toggle mode for using a first indentation for each paragraph")
  (:check-mark "v" indent-paragraphs?))

(tm-property (toggle-no-page-numbers)
  (:synopsis "Toggle mode for using standard page numbering")
  (:check-mark "v" no-page-numbers?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document updates
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define doc-update-times 1)

(define (notify-doc-update-times var val)
  (with n (cond ((string? val) (or (string->number val) 1))
                ((number? val) val)
                (else 1))
    (set! doc-update-times (min (max 1 n) 5)))) ; Just in case


(define (wait-update-current-buffer)
  (system-wait "Updating current buffer, " "please wait")
  (update-current-buffer))

(tm-define (update-document what)
  (for (.. 0 doc-update-times)       
    (delayed    ; allow typesetting/magic to happen before next update
      (:idle 1)
      (cursor-after
       (cond ((== what "all") 
              (materials-update-current-document)
              (generate-all-aux) (inclusions-gc) (picture-gc) (wait-update-current-buffer))
             ((== what "materials")
              (materials-update-current-document))
             ((== what "buffer") 
              (wait-update-current-buffer))
             (else (generate-aux what)))))))

(register-preference-callback-procedures
  (list notify-doc-update-times))
