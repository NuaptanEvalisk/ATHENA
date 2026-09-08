
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : scripts-edit.scm
;; DESCRIPTION : routines for on-the-fly evaluation of scripts
;; COPYRIGHT   : (C) 2005  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (dynamic scripts-edit)
  (:use (utils library tree)
	(utils library cursor)
        (utils edit selections)
	(dynamic scheme-runtime)
	(convert tools tmconcat)
	(dynamic scripts-drd)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Some switches
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define script-keep-input-flag? #f)
(define script-eval-math-flag? #t)

(tm-define (script-keep-input?) script-keep-input-flag?)
(tm-define (toggle-keep-input)
  (:synopsis "Toggle whether we keep the input of evaluations")
  (:check-mark "v" script-keep-input?)
  (toggle! script-keep-input-flag?))

(tm-define (script-eval-math?) script-eval-math-flag?)
(tm-define (toggle-eval-math)
  (:synopsis "Toggle whether we evaluate the innermost non-selected formulas")
  (:check-mark "v" script-eval-math?)
  (toggle! script-eval-math-flag?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Script context functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (script-defined?)
  (== (get-env "prog-scripts") "scheme"))

(tm-define (script-evaluable?)
  (or (selection-active-any?)
      (nnot (tree-innermost formula-context? #t))))

(tm-define (script-src-context? t)
  (tm-in? t '(script-eval script-result)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Style parameters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (search-tag-parameters t)
  (:require (tree-is? t 'script-input))
  (let* ((ch  (tree-ref t 0))
         (lan (if (tree-atomic? ch) (tree->string ch) "unknown"))
         (var (string-append lan "-script-input"))
         (gen "render-big-script"))         
    (search-parameters (if (style-has? var) var gen))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; In place asynchronous Scheme evaluations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (script-feed lan ses in out opts)
  (with ok? (== lan "scheme")
    (when (not ok?)
      (with m `(concat "Error: " (verbatim ,lan)
                       " is not a scripting language")
        (set-message m "Evaluate")))
    (when ok?
      (tree-set! out '(script-busy))
      (with ptr (tree->tree-pointer out)
        (with ret (lambda (r)
                    (with check (tree-pointer->tree ptr)
                      (tree-pointer-detach ptr)
                      (when (== check out)
                        (if (in? :replace opts)
                            (tree-set! out r)
                            (with-cursor (tree->path check :end)
                              (tree-cut check)
                              (if (and (in-var-math?) (tm-func? r 'math 1))
                                  (set! r (cadr r)))
                              (if (in? :declaration opts)
                                  (set! r in))
                              (insert r))))))
          (silent-feed* lan ses in ret opts))))))

(tm-define (script-eval-at where lan session in . opts)
  (script-feed lan session in where opts))

(tm-define (kbd-enter t forwards?)
  (:require (and (tree-is? t 'script-eval)
                 (xor (not forwards?)
                      (tree-is? t 1 'document))))
  (script-modified-eval noop))

(tm-define (evaluate-context? t)
  (tree-in? t '(script-input script-output)))

(tm-define (make-script-input* lan ses)
  (if (url-exists? (url-unix "$ATHENA_STYLE_PATH"
                             (string-append lan ".ts")))
      (add-style-package lan))
  (insert-go-to `(script-input ,lan ,ses "" "") '(2 0)))

(tm-define (make-script-input)
  (let* ((lan (get-env "prog-scripts"))
	 (ses (get-env "prog-session")))
    (make-script-input* lan ses)))

(tm-define (alternate-toggle t)
  (:require (tree-is? t 'script-input))
  (let* ((lan (tree->string (tree-ref t 0)))
         (session (tree->string (tree-ref t 1)))
         (in (tree->stree (tree-ref t 2)))
         (out (tree-ref t 3)))
    (script-eval-at out lan session in :math-input :simplify-output)
    (tree-assign-node! t 'script-output)
    (tree-go-to t 3 :end)))

(tm-define (alternate-toggle t)
  (:require (tree-is? t 'script-output))
  (tree-assign-node! t 'script-input)
  (tree-go-to t 2 :end))

(tm-define (kbd-enter t forwards?)
  (:require (or (tree-is? t 'script-output)
                (and (tree-is? t 'script-input)
                     (not (tree-is? t :up 'inactive)))))
  (cond ((tree-is? t 'script-output)
         (alternate-toggle t))
        ((xor (not forwards?) (tree-is? t 2 'document))
         (alternate-toggle t))
        (else
         (if (not (tree-is? t 2 'document))
             (tree-set t 2 `(document ,(tree-ref t 2))))
         (insert-return))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Operate on current selection or formula
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (script-get-input)
  (let* ((lan (get-env "prog-scripts"))
	 (session (get-env "prog-session")))
    (cond ((selection-active-any?)
	   (with sel (tree->stree (selection-tree))
	     (clipboard-cut "primary")
	     sel))
	  ((tree-innermost script-src-context?)
	   (let* ((t (tree-innermost script-src-context?))
		  (input (tree->stree (tree-ref t 0))))
	     (tree-cut t)
	     input))
	  ((and (tree-innermost formula-context? #t) script-eval-math-flag?)
	   (let* ((t (tree-innermost formula-context? #t))
		  (input (tree->stree t)))
	     (tree-cut t)
	     input))
	  (else #f))))

(define (script-modified-eval fun . opts)
  (when (script-defined?)
    (let* ((lan (get-env "prog-scripts"))
	   (session (get-env "prog-session"))
	   (input (script-get-input)))
      (when input
	;;(display* "Evaluating " input "\n")
	(insert-go-to `(script-status "") '(0 0))
	(fun)
	(insert input)
	(let* ((t (tree-innermost 'script-status))
	       (cmd (tree->stree (tree-ref t 0)))
	       (declaration? #f))
	  ;;(display* "t= " t "\n")
	  ;;(display* "cmd= " cmd "\n")
	  (cond ((and (func? input 'concat) (in? '(script-assign) input))
		 (set! declaration? #t))
		((script-keep-input?)
		 (tree-set! t `(script-result ,cmd (script-busy)))
		 (set! t (tree-ref t 1))
		 (tree-go-to t :end)))
	  (if declaration?
	      (script-eval-at t lan session cmd
			      :math-input :declaration)
	      (script-eval-at t lan session cmd
			      :math-input :simplify-output))))
      (when (not input)
	(if (not-in-session?) (make 'script-eval))
	(fun)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; High-level evaluation and function application in Scheme
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (insert-function fun)
  (insert fun)
  (if (in-var-math?)
      (insert-raw-go-to '(concat (left "(") (right ")")) '(0 1))
      (insert-raw-go-to "()" '(1))))

(tm-define (script-eval)
  (script-modified-eval noop))


(tm-define (script-apply fun . opts)
  (if (and (in? opts '(() (1))) (not-in-session?))
      (script-modified-eval (lambda () (insert-function fun)))
      (let* ((n (if (null? opts) 1 (car opts)))
	     (input (script-get-input)))
	;;(display* "Script apply " fun ", " n "\n")
	(when input
	  (if (not-in-session?) (make 'script-eval))
	  (insert-function fun)
	  (insert input)
	  (insert ",")
	  (repeat (- n 2) (insert-raw-go-to "," '(0))))
	(when (not input)
	  (if (not-in-session?) (make 'script-eval))
	  (insert-function fun)
	  (repeat (- n 1) (insert-raw-go-to "," '(0)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Scripts via forms
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (script-background-eval in . opts)
  (let* ((lan (get-env "prog-scripts"))
	 (ses (get-env "prog-session")))
    (when (== lan "scheme")
      (silent-feed* lan ses in noop opts))))

(tm-define (widget->script cas-var id)
  (with cmd `(concat ,cas-var ":" ,(tree->stree (widget-ref id)))
    ;; FIXME: only works for Maxima for the moment
    (script-background-eval cmd :math-input :simplify-output)))

(define (script-widget-eval id in . opts)
  (let* ((lan (get-env "prog-scripts"))
	 (ses (get-env "prog-session"))
	 (prefix widget-prefix))
    (when (== lan "scheme")
      (with return (lambda (r)
		     (widget-with prefix
		       (widget-set! id r)))
	(silent-feed* lan ses in return opts)))))

(tm-define (script->widget id cas-expr)
  (script-widget-eval id cas-expr :math-input :simplify-output))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Converters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (converter-context? t)
  (tree-in? t '(converter-input converter-output)))

(tm-define (kbd-enter t forwards?)
  (:require (and (tree-is? t 'converter-eval)
                 (xor (not forwards?)
                      (tree-is? t 1 'document))))
  (let* ((format (string-append (tree->string (tree-ref t 0)) "-snippet"))
         (in (texmacs->code (tree-ref t 1)))
         (mode (get-env-tree-at "mode" (rcons (tree->path t) 0))))
    (tree-select t)
    (clipboard-cut "primary")
    (if (and (== format "latex-snippet") (tm-equal? mode "math"))
        (with c (convert (string-append "$" in "$") format "texmacs-tree")
          (if (tm-func? c 'math 1)
              (insert (tm-ref c 0))
              (insert (convert in format "texmacs-tree"))))
        (insert (convert in format "texmacs-tree")))))

(tm-define (kbd-control-enter t shift?)
  (:require (tree-is-buffer? t))
  (script-eval))


(tm-define (alternate-toggle t)
  (:require (tree-is? t 'converter-input))
  (let* ((format (string-append (tree->string (tree-ref t 0)) "-snippet"))
         (in (texmacs->code (tree-ref t 1))))
    (tree-set! t 2 (convert in format "texmacs-tree"))
    (tree-assign-node! t 'converter-output)
    (tree-go-to t 2 :end)))

(tm-define (alternate-toggle t)
  (:require (tree-is? t 'converter-output))
  (tree-assign-node! t 'converter-input)
  (tree-go-to t 1 :end))

(tm-define (kbd-enter t forwards?)
  (:require (or (tree-is? t 'converter-output)
                (and (tree-is? t 'converter-input)
                     (not (tree-is? t :up 'inactive)))))
  (cond ((tree-is? t 'converter-output)
         (alternate-toggle t))
        ((xor (not forwards?) (tree-is? t 1 'document))
         (alternate-toggle t))
        (else
         (if (not (tree-is? t 1 'document))
             (tree-set t 1 `(document ,(tree-ref t 1))))
         (insert-return))))

(tm-define (kbd-remove t forwards?)
  (:require (and (or (tree-is? t 'converter-input)
                     (tree-is? t 'converter-eval))
                 (tm-ref t 1)
                 (tree-empty? (tree-ref t 1))))
  (remove-structure-upwards))
