
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : math-edit.scm
;; DESCRIPTION : editing mathematics
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module
 (math math-edit)
 (:use (utils library tree) (utils library cursor)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Combinatorial-number variants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define
 (kbd-variant t forwards?)
 (:require (tree-in? t (node-group-members "combinatorial-number-tag")))
 (variant-circulate t forwards?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special customizations inside formulas and equations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (kbd-enter t shift?)
  (:require (tree-is? t 'math))
  (go-end-of 'math)
  (insert-return))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured editing of roots
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (sqrt-toggle t)
  (when (tree-is? t 'sqrt)
    (cond ((== (tree-arity t) 1)
           (tree-insert! t 1 '(""))
           (tree-go-to t 1 0))
          ((== (tree-arity t) 2)
           (tree-remove! t 1 1)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured editing of scripts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (script-context? t)
  (tree-in? t '(lsub lsup rsub rsup)))

(tm-define (script-only-script? t)
  (and (tree-is? (tree-up t) 'concat)
       (not (or (and (tree-ref t :previous)
                     (script-context? (tree-ref t :previous)))
                (and (tree-ref t :next)
                     (script-context? (tree-ref t :next)))))))

(tm-define (variant-circulate t forward?)
  (:require (script-context? t))
  (when (script-only-script? t)
    (cond ((tree-is? t 'lsub) (variant-set t 'lsup))
          ((tree-is? t 'lsup) (variant-set t 'lsub))
          ((tree-is? t 'rsub) (variant-set t 'rsup))
          ((tree-is? t 'rsup) (variant-set t 'rsub)))))

(tm-define (structured-insert-vertical t downwards?)
  (:require (tree-in? t '(lsub rsub)))
  (when (and (not downwards?) (script-only-script? t))
    (tree-go-to t :end)
    (cond ((tree-is? t 'lsub) (make 'lsup))
          ((tree-is? t 'rsub) (make 'rsup)))))

(tm-define (structured-insert-vertical t downwards?)
  (:require (tree-in? t '(lsup rsup)))
  (when (and downwards? (script-only-script? t))
    (tree-go-to t :end)
    (cond ((tree-is? t 'lsup) (make 'lsub))
          ((tree-is? t 'rsup) (make 'rsub)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured editing of wide accents
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define wide-list-1
  '("̃" "̂" "̅" "⃗" "̌" "̆" "̑"))

(define wide-list-2
  '("́" "̀" "̇" "̈" "⃛" "⃜" "̊"))

(define wide-list-3
  '("⏞" "⏟"
    "⏜" "⏝"
    "⎴" "⎵"))

(define wide-list-4
  '("⏟" "⏞"
    "⏝" "⏜"
    "⎵" "⎴"))

(define wide-list-5
  '("⃗" "⃖"
    "⃡" "̅"))

(define (wide-accent-node t)
  (with a (tree-ref t 1)
    (if (and (tree-is? a 'with) (= (tree-arity a) 3)
             (== (tree->string (tree-ref a 0)) "math-accent-stretch")
             (== (tree->string (tree-ref a 1)) "true"))
        (tree-ref a 2)
        a)))

(define (wide-accent-set! t value)
  (with a (tree-ref t 1)
    (if (and (tree-is? a 'with) (= (tree-arity a) 3)
             (== (tree->string (tree-ref a 0)) "math-accent-stretch")
             (== (tree->string (tree-ref a 1)) "true"))
        (tree-set! a 2 value)
        (tree-set! t 1 value))))

(tm-define (variant-circulate t forward?)
  (:require (tree-in? t '(wide wide*)))
  (with a (wide-accent-node t)
    (when (tree-atomic? a)
      (with s (tree->string a)
      (and-with i (list-find-index wide-list-1 (lambda (x) (== x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length wide-list-1))
          (wide-accent-set! t (list-ref wide-list-1 j))))
      (and-with i (list-find-index wide-list-2 (lambda (x) (== x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length wide-list-2))
          (wide-accent-set! t (list-ref wide-list-2 j))))
      (and-with i (list-find-index wide-list-3 (lambda (x) (== x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length wide-list-3))
          (wide-accent-set! t (list-ref wide-list-3 j))))
      (and-with i (list-find-index wide-list-4 (lambda (x) (== x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length wide-list-4))
          (wide-accent-set! t (list-ref wide-list-4 j))))
      (and-with i (list-find-index wide-list-5 (lambda (x) (== x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length wide-list-5))
          (wide-accent-set! t (list-ref wide-list-5 j))))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Modifying the shape of brackets
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define lbrackets
  '("(" "[" "{" "⟨" "|" "‖" "⌊" "⌈" "⟦"))

(define mbrackets
  '("|" "‖" "/" "\\"))

(define rbrackets
  '(")" "]" "}" "⟩" "|" "‖" "⌋" "⌉" "⟧"))

(define (bracket-circulate t forward? brackets)
  (cond ((and (tree-in? t '(around around*))
              (== (tree-arity t) 3))
         (bracket-circulate (tree-ref t 0) forward? lbrackets)
         (bracket-circulate (tree-ref t 2) forward? rbrackets))
        ((and (tree-is? t 'left) (> (tree-arity t) 1))
         (bracket-circulate (tree-ref t 0) forward? lbrackets))
        ((and (tree-is? t 'mid) (> (tree-arity t) 1))
         (bracket-circulate (tree-ref t 0) forward? mbrackets))
        ((and (tree-is? t 'right) (> (tree-arity t) 1))
         (bracket-circulate (tree-ref t 0) forward? rbrackets))
        ((and (tree-atomic? t)
              (in? (tree->string t) brackets))
         (let* ((s (tree->string t))
                (i (list-find-index brackets (lambda (x) (== x s))))
                (j (modulo (+ i (if forward? 1 -1)) (length brackets)))
                (r (list-ref brackets j)))
           (tree-assign t r)))))

(tm-define (variant-circulate t forward?)
  (:require (tree-in? t '(left mid right around around*)))
  (bracket-circulate t forward? mbrackets))

(define bigops
  '("∫" (with "math-big-limits" "true" "∫") "⨍"
    "∮" (with "math-big-limits" "true" "∮")
    "∑" "∏" "∐"
    "⋂" "⋃" "⨅" "⨆"
    "⋁" "⋀" "⋎" "⋏"
    "⨀" "⨂" "⨁"
    "▵" "▿" "□" "∥" "⫴"))

(tm-define (variant-circulate t forward?)
  (:require (tree-is? t 'big-around))
  (when (== (tree-arity t) 2)
    (with s (tm->stree (tree-ref t 0))
      (and-with i (list-find-index bigops (lambda (x) (equal? x s)))
        (with j (modulo (+ i (if forward? 1 -1)) (length bigops))
          (tree-set! t 0 (list-ref bigops j)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Modifying the dimension of brackets
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (bracket-size-increase t by)
  (cond ((and (tree-in? t '(left mid right))
              (>= (tree-arity t) 2))
         (bracket-size-increase (tree-ref t 1) by))
        ((and (tree-in? t '(around around*))
              (== (tree-arity t) 3))
         (when (tree-atomic? (tree-ref t 0))
           (tree-set t 0 `(left ,(tree-ref t 0) "0")))
         (when (tree-atomic? (tree-ref t 2))
           (tree-set t 2 `(right ,(tree-ref t 2) "0")))
         (bracket-size-increase (tree-ref t 0) by)
         (bracket-size-increase (tree-ref t 2) by)
         (when (tm-equal? (tree-ref t 0 1) "0")
           (tree-set t 0 (tree-ref t 0 0)))
         (when (tm-equal? (tree-ref t 2 1) "0")
           (tree-set t 2 (tree-ref t 2 0))))
        ((tree-integer? t)
         (let* ((old (tree->number t))
                (new (+ old by)))
           (tree-set t (number->string new))))
        ((tm-length? t)
         (length-increase t by))))

(tm-define (geometry-vertical t down?)
  (:require (tree-in? t '(left mid right around around*)))
  (with inc (if down? -1 1)
    (bracket-size-increase t inc)))

(define (bracket-size-reset t)
  (when (and (tree-in? t '(left mid right)) (>= (tree-arity t) 1))
    (tree-set t (tree-ref t 0))))

(tm-define (geometry-default t)
  (:require (tree-in? t '(around around*)))
  (when (== (tree-arity t) 3)
    (bracket-size-reset (tree-ref t 0))
    (bracket-size-reset (tree-ref t 2))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Not necessarily matching brackets
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (find-non-bracket t)
  (cond ((not (tree-up t)) t)
        ((tm-in? t '(around around* big-around temp-slot))
         (find-non-bracket (tree-up t)))
        ((tm-in? (tree-up t) '(concat around around* big-around temp-slot))
         (find-non-bracket (tree-up t)))
        (else t)))

(define (find-and-remove-temp-slot x)
  ;;(display* "x= " x "\n")
  (cond ((tree-atomic? x) #f)
        ((tree-is? x 'temp-slot)
         (tree-go-to x 0 0)
         (remove-structure-upwards))
        ((tree? x)
         (list-or (map find-and-remove-temp-slot (tree-children x))))
        (else #f)))

(tm-define (brackets-refresh)
  (insert-go-to '(temp-slot "") '(0 0))
  (let* ((t (find-non-bracket (cursor-tree)))
	 (u (tree-downgrade-brackets t #t #f))
	 (v (tree-upgrade-brackets u "math"))
         (w (tree-downgrade-big v)))
    ;;(tree-set! t v)
    (tree-set! t w)
    (find-and-remove-temp-slot t)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Correction of mathematical formulas
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (math-correct-tree t)
  (with r (manual-correct t)
    (when (!= r t)
      (tree-set! t r))))

(tm-define (math-correct-all)
  (:synopsis "Correct selected formula or all formulas in document")
  (if (selection-active-any?)
      (if (== (selection-tree) (path->tree (selection-path)))
	  (math-correct-tree (path->tree (selection-path)))
	  (set-message "Only implemented for complete subtrees"
		       "correct formula"))
      (math-correct-tree (buffer-tree))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; UI-independent rules migrated from math-menu.scm
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (open-latex-formula-dialog)
  (:interactive #t)
  (with vals (native-latex-formula-dialog)
    (when (>= (length vals) 2)
      (let* ((mode (first vals))
             (input (second vals)))
        (cond ((== mode "inline")
               (let* ((wrapped (string-append "$" input "$"))
                      (t (latex->texmacs (parse-latex wrapped)))
                      (c (if (and (tree-is? t 'document) (> (tree-arity t) 0))
                             (tree-ref t 0)
                             t))
                      (body (if (and (tree-is? c 'with)
                                     (>= (tree-arity c) 2)
                                     (== (tree-ref c 0) "mode")
                                     (== (tree-ref c 1) "math"))
                                (tree-ref c (1- (tree-arity c)))
                                c)))
                 (insert `(math ,body))))
              ((== mode "display")
               (let ((t (latex->texmacs
                          (parse-latex (string-append "\\[" input "\\]")))))
                 (when (tree? t)
                   (for (c (tree-children t)) (insert c)))))
              (else
               (let ((t (latex->texmacs (parse-latex input))))
                 (when (tree? t)
                   (for (c (tree-children t)) (insert c))))))))))

(tm-define (standard-options l)
  (:require (== l 'math))
  (list :recurse "number-long-article" "math-check"))

(tm-define (standard-options l)
  (:require (== l 'math-colored))
  (list "math-ss"))

(tm-define (focus-tag-name l)
  (:require (== l 'math))
  "Inline formula")

(tm-define (focus-can-insert-remove? t)
  (:require (script-context? t))
  #t)

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(lsub lsup)))
  '(lsub lsup))

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(rsub rsup)))
  '(rsub rsup))

(tm-define (focus-can-insert-remove? t)
  (:require (tree-is? t 'sqrt))
  #f)

(tm-define (focus-tag-name l)
  (:require (in? l '(wide wide*)))
  "Wide")

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(wide wide*)))
  '(wide))

(tm-define (focus-has-preferences? t)
  (:require (tree-in? t '(around around*)))
  #t)

(tm-define (standard-options l)
  (:require (in? l '(around around*)))
  (list "math-brackets"))

(tm-define (focus-tag-name l)
  (:require (in? l '(around around*)))
  "Around")

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(around around*)))
  '(around))
