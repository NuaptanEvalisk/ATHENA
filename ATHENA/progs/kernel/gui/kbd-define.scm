
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : kbd-define.scm
;; DESCRIPTION : Definition of keyboard shortcuts/wildcards
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel gui kbd-define)
  (:use (kernel athena tm-define)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Lazy keyboard bindings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define lazy-keyboard-waiting '())
(tm-define lazy-keyboard-done (make-ahash-table))

(define (native-keyboard-module? module)
  (and (pair? module)
       (== (car module) 'native-keyboard)
       (pair? (cdr module))
       (null? (cddr module))
       (symbol? (cadr module))))

(tm-define (lazy-keyboard-provide module)
  (if (native-keyboard-module? module)
      (generic-keyboard-load-domain (symbol->string (cadr module)))
      (module-provide module)))

(tm-define (lazy-keyboard-do module mode*)
  (with mode (texmacs-mode-mode mode*)
    (set! lazy-keyboard-waiting (acons mode module lazy-keyboard-waiting))))

(tm-define-macro (lazy-keyboard module . modes)
  (for-each (lambda (mode) (lazy-keyboard-do module mode)) modes)
  `(delayed
     (:idle 250)
     (when (not (ahash-ref lazy-keyboard-done ',module))
       (lazy-keyboard-provide ',module)
       (ahash-set! lazy-keyboard-done ',module #t))))

(define lazy-force-all? #f)
(define lazy-force-busy? #f)
(define kbd-inverse-warmup-running? #f)

(define (lazy-keyboard-force-do l)
  (cond ((null? l) l)
	((ahash-ref lazy-keyboard-done (cdar l))
	 (lazy-keyboard-force-do (cdr l)))
	((or lazy-force-all? (texmacs-in-mode? (caar l)))
         (lazy-keyboard-provide (cdar l))
	 (ahash-set! lazy-keyboard-done (cdar l) #t)
	 (lazy-keyboard-force-do (cdr l)))
	(else (cons (car l) (lazy-keyboard-force-do (cdr l))))))

(tm-define (lazy-keyboard-force . opt)
  (set! lazy-force-all? (or lazy-force-all? (nnull? opt)))
  (when (not lazy-force-busy?)
    (set! lazy-force-busy? #t)
    (let* ((l1 (reverse lazy-keyboard-waiting))
	   (l2 (lazy-keyboard-force-do l1)))
      (set! lazy-keyboard-waiting (reverse l2))
      (set! lazy-force-busy? #f)
      (when (null? lazy-keyboard-waiting)
	(set! lazy-force-all? #f))
      (when (and lazy-force-all? (nnull? lazy-keyboard-waiting))
	(lazy-keyboard-force #t)))))

(define (lazy-keyboard-drop-done l)
  (cond ((null? l) '())
        ((ahash-ref lazy-keyboard-done (cdar l))
         (lazy-keyboard-drop-done (cdr l)))
        (else l)))

(define (kbd-inverse-warmup-step)
  (if lazy-force-busy?
      (delayed (:idle 10) (kbd-inverse-warmup-step))
      (begin
        (set! lazy-keyboard-waiting
              (lazy-keyboard-drop-done lazy-keyboard-waiting))
        (if (null? lazy-keyboard-waiting)
            (begin
              (set! kbd-inverse-warmup-running? #f)
              (update-menus))
            (let* ((entry (car lazy-keyboard-waiting))
                   (module (cdr entry)))
              (set! lazy-keyboard-waiting (cdr lazy-keyboard-waiting))
              (set! lazy-force-busy? #t)
              (dynamic-wind
                (lambda () (noop))
                (lambda ()
                  (lazy-keyboard-provide module)
                  (ahash-set! lazy-keyboard-done module #t))
                (lambda () (set! lazy-force-busy? #f)))
              (delayed (:idle 1) (kbd-inverse-warmup-step)))))))

(tm-define (kbd-start-inverse-warmup)
  (:synopsis "Populate the inverse keyboard cache after the first window")
  (when (not kbd-inverse-warmup-running?)
    (set! kbd-inverse-warmup-running? #t)
    (delayed (:idle 500) (kbd-inverse-warmup-step))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Definition of keyboard (backslashed) commands
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define kbd-command-table (make-ahash-table))

(define (kbd-set-command! key im)
  (ahash-set! kbd-command-table key im))

(tm-define (kbd-get-command key)
  (lazy-keyboard-force)
  (ahash-ref kbd-command-table key))

(tm-define (kbd-command-pre arg)
  (:synopsis "Helper routine for kbd-commands macro")
  (with (cmd help . action) arg
    (list cmd help (list 'unquote `(lambda () ,@action)))))

(tm-define (kbd-command arg)
  (:synopsis "Helper routine for kbd-commands macro")
  (with (cmd help action) arg
    (kbd-set-command! cmd (cons help action))))

(tm-define-macro (kbd-commands . l)
  (:synopsis "Add backslashed commands in @l to keyboard mapping")
  `(for-each kbd-command ,(list 'quasiquote (map kbd-command-pre l))))

(tm-define (emulate-keyboard k)
  (delayed (raw-emulate-keyboard k)))
