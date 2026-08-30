;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : ui-text.scm
;; DESCRIPTION : Direct English interface text formatting
;; COPYRIGHT   : (C) 2026 The ATHENA developers
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel gui ui-text)
  (:use (kernel gui gui-markup)))

(define (reformat-arg val)
  (cond ((string? val) val)
        ((number? val) (number->string val))
        ((tree? val) val)
        ((url? val) `(verbatim ,(url->string val)))
        ((and (pair? val) (symbol? (car val))) val)
        (else (object->string val))))

; Menu and widget markup use the native ui-text formatter directly.
(tm-define (replace origstr . vals)
  (:synopsis "Format an English interface string with arguments")
  (ui-text `(replace ,origstr ,@(map reformat-arg vals))))
