;; Verify that Reload/Revert never touches the GUI buffer registry from a
;; BufferActor, including the continuation of the modified-buffer prompt.
(define root (cadr (command-line)))
(define global-calls 0)
(define in-global? #f)
(define confirmation #f)
(define modified? #f)
(define exists? #t)
(define current-name "/tmp/current.ath")
(define switched #f)
(define invalidated #f)
(define installed #f)
(define loaded #f)

(define (check condition message)
  (unless condition (error "Reload ownership regression" message)))

(define-macro (with var value . body)
  `(let ((,var ,value)) ,@body))
(define (== a b) (equal? a b))
(define (!= a b) (not (equal? a b)))
(define (tm->tree x) x)
(define (url->system x) x)
(define (system->url x) x)

(define (exec-global thunk)
  (set! global-calls (+ global-calls 1))
  (let ((previous in-global?))
    (dynamic-wind
      (lambda () (set! in-global? #t))
      thunk
      (lambda () (set! in-global? previous)))))

(define (require-global what)
  (check in-global? (string-append what " ran outside the global owner")))

(define (current-buffer) current-name)
(define (buffer-exists? name)
  (require-global "buffer-exists?")
  exists?)
(define (buffer-modified? name)
  (require-global "buffer-modified?")
  modified?)
(define (switch-to-buffer name)
  (require-global "switch-to-buffer")
  (set! current-name name)
  (set! switched name))
(define (url-cache-invalidate name)
  (require-global "url-cache-invalidate")
  (set! invalidated name))
(define (url-format name) "texmacs")
(define (tree-import name format)
  (require-global "tree-import")
  `(document ,name))
(define (set-message left right)
  (require-global "set-message"))
(define (buffer-set name doc)
  (require-global "buffer-set")
  (set! installed (list name doc)))
(define (load-buffer name)
  (require-global "load-buffer")
  (set! loaded name))
(define (user-confirm question default cont)
  (require-global "user-confirm")
  (set! confirmation cont))

(define (load-definition wanted)
  (call-with-input-file
    (string-append root "/ATHENA/progs/athena/athena/tm-files.scm")
    (lambda (port)
      (let loop ((form (read port)))
        (cond ((eof-object? form)
               (error "Reload ownership regression"
                      "definition not found" wanted))
              ((and (pair? form)
                    (or (eq? (car form) 'define) (eq? (car form) 'tm-define))
                    (pair? (cdr form)) (pair? (cadr form))
                    (eq? (caadr form) wanted))
               (eval (if (eq? (car form) 'tm-define)
                         (cons 'define (cdr form)) form)
                     (current-module)))
              (else (loop (read port))))))))

(for-each load-definition
  '(revert-buffer-revert-global revert-buffer-revert
    revert-buffer-global revert-buffer))

;; Ordinary Reload is initiated from BufferActor Scheme, but all registry and
;; replacement work must occur after crossing to the global owner.
(revert-buffer)
(check (= global-calls 1) "Reload did not cross through exec-global exactly once")
(check (equal? invalidated "/tmp/current.ath") "Reload did not invalidate target")
(check (equal? installed
              '("/tmp/current.ath" (document "/tmp/current.ath")))
       "Reload did not replace the requested buffer")

;; Direct revert-buffer-revert is also public and is used by developer/email
;; code, so it must be safe when invoked directly from BufferActor Scheme.
(set! invalidated #f)
(set! installed #f)
(revert-buffer-revert "/tmp/direct.ath")
(check (= global-calls 2) "direct revert did not cross through exec-global")
(check (equal? invalidated "/tmp/direct.ath") "direct revert target changed")

;; A modified buffer prompts on the global owner, but the answer may resume on
;; the BufferActor. The answer must cross globally again, and it must keep the
;; buffer captured when Reload was requested even if the active buffer changes.
(set! modified? #t)
(set! confirmation #f)
(set! invalidated #f)
(set! installed #f)
(set! current-name "/tmp/modified.ath")
(revert-buffer)
(check (= global-calls 3) "modified Reload did not start globally")
(check (procedure? confirmation) "modified Reload did not request confirmation")
(set! current-name "/tmp/other.ath")
(confirmation #t)
(check (= global-calls 4) "confirmation continuation did not return globally")
(check (equal? invalidated "/tmp/modified.ath")
       "confirmation reloaded the wrong active buffer")
(check (equal? installed
              '("/tmp/modified.ath" (document "/tmp/modified.ath")))
       "confirmation did not replace the originally requested buffer")

;; Missing buffers are loaded, but the existence query and load still belong
;; to the global owner.
(set! modified? #f)
(set! exists? #f)
(set! loaded #f)
(revert-buffer-revert "/tmp/missing.ath")
(check (= global-calls 5) "missing-buffer revert did not cross globally")
(check (equal? loaded "/tmp/missing.ath") "missing buffer was not loaded")

(display "PASS: Reload/Revert orchestration stays on the global owner\n")
