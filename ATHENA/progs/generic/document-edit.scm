
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

(define (in-source-mode?)
  (== (get-env "preamble") "true"))

(define (apply-source-tree-preferences)
  (init-env "src-style" (get-preference "source tree style"))
  (init-env "src-special" (get-preference "source tree special rendering"))
  (init-env "src-compact" (get-preference "source tree compactification"))
  (init-env "src-close" (get-preference "source tree closing style")))

(tm-define (toggle-source-mode)
  (:synopsis "Toggle source code editing mode")
  (:check-mark "v" in-source-mode?)
  (let ((new (if (string=? (get-env "preamble") "true") "false" "true")))
    (when (== new "true") (apply-source-tree-preferences))
    (init-env "preamble" new)))

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

(tm-define (document-font-display-name val)
  (with fam (font-family-main val)
    (cond ((or (== fam "bonum")
               (string-starts? fam "TeX Gyre Bonum")) "Bonum")
          ((or (== fam "pagella")
               (string-starts? fam "TeX Gyre Pagella")) "Pagella")
          ((or (== fam "schola")
               (string-starts? fam "TeX Gyre Schola")) "Schola")
          ((or (== fam "termes")
               (string-starts? fam "TeX Gyre Termes")) "Termes")
          (else (upcase-first fam)))))

(tm-define (test-init-font? val . opts)
  (== (document-font-display-name (get-init "font"))
      (document-font-display-name val)))

(tm-define (remove-font-packages)
  (with l (get-style-list)
    (with f (list-filter l (lambda (p) (not (string-ends? p "-font"))))
      (set-style-list f))))

(define (font-package-name val)
  (cond ((== val "Fira") "fira-font")
        ((== val "Linux Biolinum") "biolinum-font")
        ((== val "Linux Libertine") "libertine-font")
        (else (string-append val "-font"))))

(define (tex-gyre-document-font? val)
  (or (in? val '("Bonum" "bonum" "Pagella" "pagella"
                  "Schola" "schola" "Termes" "termes"))
      (string-starts? val "TeX Gyre Bonum")
      (string-starts? val "TeX Gyre Pagella")
      (string-starts? val "TeX Gyre Schola")
      (string-starts? val "TeX Gyre Termes")))

(define (tex-gyre-document-profile val)
  (cond ((or (in? val '("Bonum" "bonum"))
             (string-starts? val "TeX Gyre Bonum"))
         "TeX Gyre Bonum")
        ((or (in? val '("Pagella" "pagella"))
             (string-starts? val "TeX Gyre Pagella"))
         "TeX Gyre Pagella")
        ((or (in? val '("Schola" "schola"))
             (string-starts? val "TeX Gyre Schola"))
         "TeX Gyre Schola")
        ((or (in? val '("Termes" "termes"))
             (string-starts? val "TeX Gyre Termes"))
         "TeX Gyre Termes")
        (else val)))

(tm-define (init-font val . opts)
  (:check-mark "*" test-init-font?)
  (cond ((== val "TeXmacs Computer Modern")
         (init-font "roman" "roman"))
        ((and (== val "roman") (!= opts (list "roman")))
         (init-font "roman" "roman"))
        ((tex-gyre-document-font? val)
         ;; Document-level TeX Gyre choices use the same smart-font profile
         ;; representation as the native font selector.  The legacy
         ;; *-font packages route through the old logical math-font stack and
         ;; can disagree with the profile renderer for large operators.
         (init-env "font" (tex-gyre-document-profile val))
         (init-default "math-font")
         (init-env "font-family" "rm")
         (remove-font-packages))
        ((string-starts? val "Stix")
         (init-font "stix" "math-stix"))
        (else
          (init-env "font" val)
          (when (nnull? opts)
            (init-env "math-font" (car opts)))
          (init-env "font-family" "rm")
          (remove-font-packages)
          (with pack (font-package-name val)
            (with dir "$ATHENA_PATH/packages/customize/fonts"
              (when (url-exists? (url-append dir (string-append pack ".ts")))
                (init-default "font")
                (init-default "font-family")
                (add-style-package pack)))))))

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

(tm-define (test-default-document-language?)
  (null? (list-intersection (get-style-list) supported-languages)))

(tm-define (set-default-document-language)
  (:check-mark "*" test-default-document-language?)
  (let* ((old (get-style-list))
         (new (list-difference old supported-languages)))
    (when (!= new old)
      (set-style-list new))))

(tm-define (get-document-language)
  (with l (list-intersection (get-style-list) supported-languages)
    (if (null? l) (get-init "language") (car l))))

(tm-define (test-document-language? s)
  (== s (get-document-language)))

(tm-define (set-document-language lan)
  (:check-mark "*" test-document-language?)
  (let* ((old (get-style-list))
         (rem (list-difference old supported-languages))
         (new (append rem (if (== lan "english") (list) (list lan)))))
    (when (!= new old)
      (set-style-list new))))

(define (search-env-var t which)
  (cond ((nlist? t) #f)
        ((null? t) #f)
        ((match? t '(associate "language" :%1)) (caddr t))
        (else (let ((val (search-env-var (car t) which)))
                (if val val (search-env-var (cdr t) which))))))

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

(define (panorama-packets)
  (let* ((nr (nr-pages))
         (ww (get-window-width))
         (wh (get-window-height))
         (pw (get-page-width #f))
         (ph (get-page-height #f))
         (best-n 0)
         (best-f 0))
    (for (n (.. 1 (+ nr 1)))
      (let* ((r (quotient (+ nr (- n 1)) n))
             (tw (* n pw))
             (th (* r ph))
             (aw (- ww (* n 5120)))
             (ah (- wh (* r 5120)))
             (fw (/ (* 1.0 aw) tw))
             (fh (/ (* 1.0 ah) th))
             (f (min fw fh)))
        (when (or (== n 1) (> f best-f))
          (set! best-n n)
          (set! best-f f))))
    (cond ((> best-n 0) best-n)
          ((> nr 10) 10)
          (else (inexact->exact (ceiling (sqrt (* 1.0 nr))))))))

(define (test-default-page-rendering?) (test-default? "page-medium"))
(tm-define (init-default-page-rendering)
  (:check-mark "*" test-default-page-rendering?)
  (init-default "page-medium")
  (init-default "page-border")
  (init-default "page-packet")
  (init-default "page-offset")
  (notify-page-change))

(tm-define (get-init-page-rendering)
  (cond ((== (get-init "page-border") "attached") "book")
        ((!= (get-init "page-packet") "1") "panorama")
        ((and (== (get-init "page-medium") "paper")
              (nnot (tree-innermost 'slideshow))) "slideshow")
        (else (get-init "page-medium"))))

(define (test-page-rendering? s) (== (get-init-page-rendering) s))
(tm-define (init-page-rendering s)
  (:check-mark "*" test-page-rendering?)
  (when (in? s (list "paper" "papyrus"))
    (set-preference "page medium" s))
  (save-zoom (get-init-page-rendering))
  (cond ((== s "book")
         (init-env "page-medium" "paper")
         (init-env "page-border" "attached")
         (init-env "page-packet" "2")
         (init-env "page-offset" "1")
	 (notify-page-change)
	 (delayed (:idle 25) (restore-zoom s)))
        ((== s "panorama")
         (init-env "page-medium" "paper")
         (init-env "page-packet" (number->string (panorama-packets)))
         (init-default "page-border")
         (init-default "page-offset")
	 (notify-page-change)
	 (delayed (:idle 25) (fit-all-to-screen)))
        ((== s "slideshow")
         (init-env "page-medium" "paper")
         (init-default "page-packet")
         (init-default "page-border")
         (init-default "page-offset")
	 (notify-page-change)
	 (delayed (:idle 25) (restore-zoom "slideshow")))
        (else
         (init-env "page-medium" s)
         (init-default "page-border")
         (init-default "page-packet")
         (init-default "page-offset")
         (notify-page-change)
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
