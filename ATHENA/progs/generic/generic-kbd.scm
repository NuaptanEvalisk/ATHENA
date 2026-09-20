
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : generic-kbd.scm
;; DESCRIPTION : general keyboard shortcuts for all modes
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic generic-kbd)
  (:use (athena keyboard prefix-kbd)
        (utils edit variants)
        (utils edit auto-close)
        (utils library cursor)
        (generic document-edit)
        (generic generic-edit)
        (generic format-drd)
        (source source-edit)
        (athena athena tm-files)
        (athena athena tm-print)
        (athena athena tm-vault)
        (athena menus file-menu)
        (doc help-funcs)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; General shortcuts for all modes
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (escape-symbol-insert action)
  (with dispatch (escape-symbol-native-dispatch action)
    (cond ((== dispatch #t) (noop))
          ((pair? dispatch) (apply (eval (car dispatch)) (cdr dispatch)))
          (else (key-press action)))))

(tm-define (open-escape-symbol-picker)
  (:interactive #t)
  (with action (escape-symbol-picker)
    (when (!= action "") (escape-symbol-insert action))))

(generic-keyboard-load)
