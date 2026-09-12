
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tmconcat.scm
;; DESCRIPTION : manipulation of concatenations
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert tools tmconcat))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constructor for concatenations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (tmconcat* . l)
  (:synopsis "Non-correcting constructor of horizontal concatenations.")
  (cond ((null? l) "")
	((null? (cdr l)) (car l))
	(else (cons 'concat l))))

(tm-define (tmconcat-simplify l)
  (cond ((null? l) l)
        ((tm-atomic? (car l))
         (let* ((head (tm->string (car l)))
                (tail (tmconcat-simplify (cdr l))))
           (cond ((== head "") tail)
                 ((and (nnull? tail) (string? (car tail)))
                  (cons (string-append head (car tail)) (cdr tail)))
                 (else (cons head tail)))))
	((tm-func? (car l) 'concat)
         (tmconcat-simplify (append (tm-cdr (car l)) (cdr l))))
        (else (cons (car l) (tmconcat-simplify (cdr l))))))

;; (tm-define (tmconcat . in)
;;   (:synopsis "Constructor of horizontal concatenations with corrections.")
;;   (let* ((l (tmconcat-simplify in))
;; 	 (o (length (list-filter l (lambda (x) (tm-func? x 'left)))))
;; 	 (c (length (list-filter l (lambda (x) (tm-func? x 'right))))))
;;     (if (> o c) (set! l (append l (make-list (- o c) '(right ".")))))
;;     (if (< o c) (set! l (append l '(right ".") (make-list (- o c)))))
;;     (apply tmconcat* l)))

(tm-define (tmconcat . in)
  (:synopsis "Constructor of horizontal concatenations with corrections.")
  (apply tmconcat* (tmconcat-simplify in)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Replacing mathematical string by list of tokens
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (tmconcat-eat s pos plus pred?)
  (let eat ((end pos))
    (cond ((>= end (string-length s))
	   (cons (substring s pos end)
		 (tmconcat-math-sub s end)))
	  ((pred? (string-ref s end))
	   (cons (substring s pos (+ end plus))
		 (tmconcat-math-sub s (+ end plus))))
	  (else (eat (+ end 1))))))

(define (tmconcat-math-sub s pos)
  (if (>= pos (string-length s)) '()
      (with c (string-ref s pos)
	(cond ((== c #\<)
	       (tmconcat-eat s pos 1 (lambda (c) (== c #\>))))
	      ((char-numeric? c)
	       (tmconcat-eat s pos 0 (lambda (c) (not (char-numeric? c)))))
	      ((and (char-alphabetic? c)
		    (< (+ pos 1) (string-length s))
		    (char-alphabetic? (string-ref s (+ pos 1))))
	       (tmconcat-eat s pos 0 (lambda (c) (not (char-alphabetic? c)))))
	      (else (cons (substring s pos (+ pos 1))
			  (tmconcat-math-sub s (+ pos 1))))))))

(tm-define (tmconcat-tokenize-math s)
  (:type (-> string (list string)))
  (:synopsis "Decompose mathematical string @s into list of tokens")
  (tmconcat-math-sub s 0))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Simplification of weak tabs
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (tab? tab) (func? tab 'htab))
(define (tab-left? tab) (and (func? tab 'htab 2) (== (caddr tab) "last")))
(define (tab-right? tab) (and (func? tab 'htab 2) (== (caddr tab) "first")))
(define (tab-weak tab) `(,(car tab) ,(cadr tab) "0"))

(define (has-tab? x)
  (cond ((tab? x) #t)
        ((func? x 'with) (has-tab? (cAr x)))
        ((func? x 'concat) (list-or (map has-tab? (cdr x))))
        (else #f)))

(define (has-tab-before? x pos)
  (cond ((null? pos) #f)
        ((list-or (map has-tab? (sublist x 0 (car pos)))) #t)
        (else (has-tab-before? (list-ref x (car pos)) (cdr pos)))))

(define (has-tab-after? x pos)
  (cond ((null? pos) #f)
        ((list-or (map has-tab? (sublist x (+ (car pos)) (length x)))) #t)
        (else (has-tab-after? (list-ref x (car pos)) (cdr pos)))))

(define (simplify-tabs-sub x pos i in)
  (if (null? x) x
      (cons (simplify-tabs (car x) (rcons pos i) in)
            (simplify-tabs-sub (cdr x) pos (+ i 1) in))))

(define (simplify-tabs x pos in)
  ;;(display* "Simplify " x ", " pos "\n")
  ;; FIXME: in our recursive descent, we might want to treat
  ;; other constructs besides with, such as surround
  (cond ((npair? x) x)
        ((tab-left? x)
         (if (has-tab-after? in pos) "" (tab-weak x)))
        ((tab-right? x)
         (if (has-tab-before? in pos) "" (tab-weak x)))
        ((func? x 'with)
         (with spos (rcons pos (- (length x) 1))
           (rcons (cDr x) (simplify-tabs (cAr x) spos in))))
        ((func? x 'concat)
         (simplify-tabs-sub x pos 0 in))
        (else x)))

(tm-define (tmconcat-simplify-tabs l)
  (:type (forall T (-> (list T) (list T))))
  (:synopsis "Rewrite weak left and write tabs in concatenation @l")
  (with c (cons 'concat l)
    (cdr (simplify-tabs c (list) c))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Replacing tab information by alignment information
;; The alignment is expressed using the !left, !middle and !right tags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (tmconcat-tabs-make head tail where next)
  (with (first . others) (tmconcat-tabs-sub (cdar tail) (cdr tail) next)
    (if (== where next)
	`((,where ,@head ,@(cdr first)) ,@others)
	`((,where ,@head) ,first ,@others))))

(define (tmconcat-tabs-sub head tail where)
  (cond ((null? tail) `((,where ,@head)))
	((and (== where '!left) (> (length tail) 1))
	 (tmconcat-tabs-make head tail where '!middle))
	(else (tmconcat-tabs-make head tail where '!right))))

(tm-define (tmconcat-structure-tabs l)
  (:type (forall T (-> (list T) (list T))))
  (:synopsis "Structure tabs in concatenation @l")
  ;;(display* "**** l << " l "\n")
  (set! l (tmconcat-simplify-tabs l))
  ;;(display* "**** l << " l "\n")
  (with r (list-scatter l tab? #t)
    (if (null? (cdr r)) l
        (tmconcat-tabs-sub (car r) (cdr r) '!left))))
