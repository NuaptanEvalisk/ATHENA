
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-define.scm
;; DESCRIPTION : Macros for defining TeXmacs functions
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel athena tm-define))

(let ((old-procedure-name procedure-name))
  (set! procedure-name
        (lambda (fun)
          (or (old-procedure-name fun)
              (%athena-procedure-name fun)))))

(define-public (procedure-sources about)
  (or (and (procedure? about)
           (%athena-definition-sources (procedure-name about)))
      (and (procedure-source about)
           (list (procedure-source about)))))

(define-public (help about)
  ;; very provisional
  (cond ((property about :synopsis)
         (property about :synopsis))
        ((procedure-documentation about)
         (procedure-documentation about))
        (else #f)))
