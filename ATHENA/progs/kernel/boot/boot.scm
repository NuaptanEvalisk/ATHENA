
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : boot.scm
;; DESCRIPTION : some global variables, public macros, on-entry, on-exit and
;;               initialization of the TeXmacs module system
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(use-modules (athena runtime))

(define (guile-a?) (equal? (scheme-dialect) "guile-a"))
(define (guile-b?) (equal? (scheme-dialect) "guile-b"))
(define (guile-c?) (equal? (scheme-dialect) "guile-c"))
(define (guile-d?) (equal? (scheme-dialect) "guile-d"))
(define (modern-guile?) (or (guile-b?) (guile-c?) (guile-d?)))
(define guile-b-c? modern-guile?)
(if (or (guile-c?) (guile-d?))
    (use-modules (ice-9 rdelim) (ice-9 pretty-print)))
(define has-look-and-feel? (lambda (x) (== x "emacs")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Redirect standard output
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define original-display display)
(define original-write write)

(define (display . l)
  "display one object on the standard output or a specified port."
  (if (or (null? l) (not (null? (cdr l))))
      (apply original-display l)
      (tm-output (display-to-string (car l)))))

(define (write . l)
  "write an object to the standard output or a specified port."
  (if (or (null? l) (not (null? (cdr l))))
      (apply original-write l)
      (tm-output (object->string (car l)))))

(define texmacs-error-port
  (make-soft-port
   (vector (lambda (c) (tm-errput (char->string c)))
           (lambda (s) (tm-errput s))
           (lambda () (noop))
           (lambda () #\?)
           (lambda () (noop)))
   "w"))

(set-current-error-port texmacs-error-port)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; On-entry and on-exit macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (quit-TeXmacs-scheme) (noop))

(define-macro (on-entry . cmd)
  `(begin ,@cmd))

(define-macro (on-exit . cmd)
  `(let ((former quit-TeXmacs-scheme))
     (set! quit-TeXmacs-scheme (lambda () ,@cmd (former)))))
