;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; In-process Scheme evaluation for sessions and calculation fields.
;; Derived from the former evaluator, (C) 1999-2009 Joris van der Hoeven.
;; GNU GPL version 3 or later; see LICENSE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (dynamic scheme-runtime)
  (:use (utils library tree) (utils library cursor)))

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

(tm-define (scheme-prompt lan ses) "Scheme] ")

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

(tm-define (scheme-eval-field lan ses in out next opts mode
                             coherent? output remove-output)
  (when (!= lan "scheme") (error "Only Scheme evaluation is supported"))
  (tree-assign! out '(document (script-busy)))
  (let* ((source (if (tm? in) (tm->stree in) in))
         (out-ptr (tree->tree-pointer out))
         (next-ptr (tree->tree-pointer next))
         (busy-ptr (tree->tree-pointer (tree-ref out 0))))
    (delayed
      (dynamic-wind
        noop
        (lambda ()
          (let* ((dest (tree-pointer->tree out-ptr))
                 (next (tree-pointer->tree next-ptr))
                 (busy (tree-pointer->tree busy-ptr)))
            (when (and (tree? dest) (tree? next) (tree? busy)
                       (tree->path busy) (coherent? dest next))
              (with start (texmacs-time)
                (when (and (!= source :start) (not (tree-empty? source)))
                  (tree-set dest :up 0 (scheme-prompt lan ses))
                  (with r (scheme-eval source mode)
                    (if (not (tm-func? r 'document))
                        (set! r (tree 'document r)))
                    (when (and (tree->path busy) (coherent? dest next))
                      (output dest r))))
                (when (and (tree->path busy) (coherent? dest next))
                  (let* ((dt (- (texmacs-time) start))
                         (ts (if (< dt 1000)
                                 (string-append (number->string dt) " msec")
                                 (string-append (number->string (/ dt 1000.0)) " sec"))))
                    (if (and (in? :timings opts) (>= dt 1))
                        (tree-set! busy `(timing ,ts))
                        (tree-remove! dest (tree-index busy) 1)))
                  (when (tree-empty? dest) (remove-output (tree-up dest))))))))
        (lambda ()
          (tree-pointer-detach out-ptr)
          (tree-pointer-detach next-ptr)
          (tree-pointer-detach busy-ptr))))))

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
