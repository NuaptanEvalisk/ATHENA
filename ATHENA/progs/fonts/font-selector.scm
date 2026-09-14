;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : font-selector.scm
;; DESCRIPTION : native font selector bridge
;; COPYRIGHT   : (C) 2012  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (fonts font-selector)
  (:use (generic format-edit)
        (generic document-edit)))

(define (initial-font-data getter)
  (let* ((fam (font-family-main (getter "font")))
         (var (getter "font-family"))
         (ser (getter "font-series"))
         (sh  (getter "font-shape"))
         (sz  (getter "font-base-size"))
         (lf  (logical-font-private fam var ser sh))
         (fn  (logical-font-search-exact lf)))
    (list (or (car fn) "TeXmacs Computer Modern")
          (or (cadr fn) "Regular")
          (or sz "10"))))

(define (font-selection-changes getter family style size profile)
  (let* ((fn (logical-font-public family style))
         (l '()))
    (when (!= size (getter "font-base-size"))
      (set! l (cons* "font-base-size" size l)))
    (when (!= (logical-font-shape fn) (getter "font-shape"))
      (set! l (cons* "font-shape" (logical-font-shape fn) l)))
    (when (!= (logical-font-series fn) (getter "font-series"))
      (set! l (cons* "font-series" (logical-font-series fn) l)))
    (when (!= (logical-font-variant fn) (getter "font-family"))
      (set! l (cons* "font-family" (logical-font-variant fn) l)))
    (when (!= profile (getter "font"))
      (set! l (cons* "font" profile l)))
    l))

(define (native-font-selector-window getter setter title)
  (let* ((init (initial-font-data getter))
         (profile (getter "font"))
         (res (native-font-selector (car init) (cadr init) (caddr init)
                                    profile title)))
    (when (list-4? res)
      (let ((changes (font-selection-changes getter
                                              (car res)
                                              (cadr res)
                                              (caddr res)
                                              (cadddr res))))
        (when (nnull? changes)
          (setter changes)))
      (keyboard-focus-on "canvas"))))

(tm-define (open-font-selector)
  (:interactive #t)
  (native-font-selector-window get-env make-multi-with "Font selector"))

(tm-define (open-document-font-selector)
  (:interactive #t)
  (native-font-selector-window get-init init-multi "Document font selector"))

(define ((prefixed-get-init prefix) var)
  (if (init-has? (string-append prefix var))
      (get-init (string-append prefix var))
      (get-init var)))

(define ((prefixed-init-multi prefix) l)
  (when (and (nnull? l) (nnull? (cdr l)))
    (init-env (string-append prefix (car l)) (cadr l))
    ((prefixed-init-multi prefix) (cddr l))))

(tm-define (open-document-other-font-selector prefix)
  (:interactive #t)
  (native-font-selector-window (prefixed-get-init prefix)
                               (prefixed-init-multi prefix)
                               "Font selector"))
