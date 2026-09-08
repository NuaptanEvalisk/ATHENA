(import-from (dynamic session-edit) (dynamic program-edit)
             (dynamic scripts-edit) (dynamic calc-edit)
             (prog code-format) (prog python-format) (athena athena tm-codex))
(lazy-menu-force-all)

(define (check expected actual label)
  (unless (equal? expected actual)
    (error "Built-in Scheme regression" label expected actual)))

(for-each
  (lambda (name) (check #f (defined? name) name))
  '(plugin-list plugin-configure plugin-initialize plugin-eval
    connection-start connection-eval connection-write connection-stop
    lazy-plugin-force lazy-input-converter plugin-input-converters))

(check "3" (tm->stree (scheme-eval "(+ 1 2)" :session)) "Scheme arithmetic")
(check '(text (frac "1" "2"))
       (tm->stree (scheme-eval "(tree 'frac \"1\" \"2\")" :silent))
       "structured Scheme result")
(check #t (defined? 'codex-ai-completion) "AI Completion entry point")
(check #t (tm? (convert "x = 1" "python-snippet" "texmacs-tree"))
       "built-in Python source conversion")
(check #t (tm? (convert "int x = 1;" "cpp-snippet" "texmacs-tree"))
       "built-in C++ source conversion")

;; Drive delayed field jobs deterministically in this editor's execution
;; context. No external interpreter or Codex process is started.
(define pending '())
(define scheduler exec-delayed-pause)
(define (drain)
  (unless (null? pending)
    (let ((job (car pending)))
      (set! pending (cdr pending))
      (job)
      (drain))))
(dynamic-wind
  (lambda ()
    (set! exec-delayed-pause
      (lambda (job) (set! pending (append pending (list job))))))
  (lambda ()
    (let ((answer #f))
      (silent-feed* "scheme" "default" "(+ 2 3)"
        (lambda (x) (set! answer x)) '())
      (check #f answer "evaluation must be deferred")
      (drain)
      (check '(document "5") answer "silent document output")
      (silent-feed* "scheme" "default" "(+ 3 4)"
        (lambda (x) (set! answer x)) '(:simplify-output))
      (drain)
      (check "7" answer "simplified output"))
    (buffer-set-body (current-buffer)
      '(document
        (session "scheme" "test"
          (document
            (unfolded-io "Scheme] " (document "(+ 8 9)") (document ""))
            (input "Scheme] " (document ""))))))
    (let* ((body (tree-ref (buffer-tree) 0 2))
           (out (tree-ref body 0 2))
           (next (tree-ref body 1)))
      (session-feed "scheme" "test" "(+ 8 9)" out next '())
      (drain)
      (set! out (tree-ref body 0 2))
      (check '(document "17") (tree->stree out) "session field output")
      (session-feed "scheme" "test" "(+ 9 10)" out next '())
      (tree-remove! body 0 1)
      (drain)
      (check 1 (tree-arity body) "deleted field callback must be ignored")))
  (lambda () (set! exec-delayed-pause scheduler)))

(let ((before (tree->stree (buffer-tree))))
  (check #t
    (catch #t (lambda () (make-session "python" "default") #f)
      (lambda args #t))
    "external Session must be rejected")
  (check before (tree->stree (buffer-tree)) "rejection must not edit document"))

(buffer-set-body (current-buffer) '(document "Built-in Scheme works."))
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file
  (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
