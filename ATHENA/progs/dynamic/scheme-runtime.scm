;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; In-process Scheme evaluation for sessions and calculation fields.
;; Derived from the former evaluator, (C) 1999-2009 Joris van der Hoeven.
;; GNU GPL version 3 or later; see LICENSE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (dynamic scheme-runtime)
  (:use (utils library tree) (utils library cursor)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Internal evaluator
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (replace-newline s)
  (with l (string-tokenize-by-char s #\newline)
    (if (<= (length l) 1) s
        (tm->tree `(document ,@l)))))

(define (var-object->string t)
  (with s (object->string t)
    (if (== s "#<unspecified>") ""
        (replace-newline (string->tmstring s)))))

(define (eval-string-with-catch s)
  (catch #t
    (lambda () (eval (string->object s)))
    (lambda (key msg . err-msg)
      (let* ((msg (car err-msg))
             (args (cadr err-msg))
             (err-msg
               (if (list? args) (eval (apply format #f msg args)) msg)))
        (stree->tree `(errput ,err-msg))))))

(define (error-tree? t)
  (and (tree? t) (tree-is? t 'errput)))

(tm-define (scheme-eval t mode)
  (let* ((s (texmacs->code t "iso-8859-1"))
         (r (eval-string-with-catch s)))
    (cond ((and (tree? r) (error-tree? r))
           (tree-copy r))
          ((tree? r)
           (tree 'text (tree-copy r)))
          ((and (tm? r) (== mode :silent))
           (tree-copy (tm->tree r)))
          (else (var-object->string r)))))

(tm-define (scheme-output-std-simplify name t)
  (cond ((or (func? t 'document 0) (func? t 'concat 0)) "")
        ((or (func? t 'document 1) (func? t 'concat 1))
         (scheme-output-simplify name (cadr t)))
        ((and (or (func? t 'document) (func? t 'concat))
              (in? (cadr t) '("" " " "  ")))
         (scheme-output-simplify name (cons (car t) (cddr t))))
        ((and (or (func? t 'document) (func? t 'concat))
              (in? (cAr t) '("" " " "  ")))
         (scheme-output-simplify name (cDr t)))
        ((match? t '(with "mode" "math" :%1))
         `(math ,(scheme-output-simplify name (cAr t))))
        ((func? t 'with)
         (rcons (cDr t) (scheme-output-simplify name (cAr t))))
        (else t)))

(tm-define (scheme-output-simplify name t)
  (scheme-output-std-simplify name t))

(tm-define (scheme-postprocess lan ses r opts)
  (if (in? :simplify-output opts) (scheme-output-simplify lan r) r))

;; Delayed commands retain the caller's execution context. There is no
;; cross-document transport queue or shared mutable connection state.
(tm-define (silent-feed* lan ses in return opts)
  (when (!= lan "scheme") (error "Only Scheme evaluation is supported"))
  (with source (if (tm? in) (tm->stree in) in)
    (delayed
      (with result (if (tree-empty? source) '(document)
                      (tm->stree (scheme-eval source :silent)))
        (unless (func? result 'document) (set! result `(document ,result)))
        (return (scheme-postprocess lan ses result opts))))))

(define (cell-context-inside-sub? t which)
  (or (and (list? which) (tree-in? t which))
      (and (nlist? which) (tree-is? t which))
      (and (tree-in? t '(table tformat document))
           (cell-context-inside-sub? (tree-up t) which))))

(define (cell-context-inside? t which)
  (and (tree-is? t 'cell)
       (tree-is? t :up 'row)
       (cell-context-inside-sub? (tree-ref t :up :up)  which)))

(tm-define (formula-context? t)
  (with u (tree-up t)
    (and u (or (tree-in? u '(math equation equation*))
               (match? u '(with "mode" "math" :%1))
               (cell-context-inside? u '(eqnarray eqnarray*))))))

(tm-define (in-var-math?)
  (let* ((t1 (tree-innermost formula-context? #t))
         (t2 (tree-innermost 'text)))
    (and (nnot t1) (or (not t2) (tree-inside? t1 t2)))))
