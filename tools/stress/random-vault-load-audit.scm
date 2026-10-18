;; The driver supplies an isolated profile and a private copy of the Vault.
;; All editor operations run on the document's owning BufferActor.
(define audit-result (getenv "ATHENA_LOAD_AUDIT_RESULT"))
(define audit-messages (getenv "ATHENA_LOAD_AUDIT_MESSAGES"))
(define audit-source (string->url (getenv "ATHENA_LOAD_AUDIT_SOURCE")))
(define audit-root (getenv "ATHENA_LOAD_AUDIT_VAULT"))
(define (audit-phase text)
  (call-with-output-file (string-append audit-result ".phase")
    (lambda (port) (display text port)))
  (display "ATHENA-LOAD-AUDIT: ") (display text) (newline) (force-output))
(define (audit-finish result)
  (audit-phase "collect diagnostics")
  (call-with-output-file audit-messages
    (lambda (port)
      (for-each
        (lambda (message)
          (display (tree->string (tree-ref message 0)) port)
          (display ": " port)
          (display (tree->string (tree-ref message 1)) port)
          (newline port))
        (tree-children (get-debug-messages "Error messages" 1000000)))))
  (call-with-output-file audit-result (lambda (port) (write result port)))
  (quit-TeXmacs))
(define (audit-fail . args)
  (exec-global (lambda () (audit-finish args))))
(define (audit-typeset final? attempts)
  (unless
    (exec-buffer audit-source
      (lambda ()
        (catch #t
          (lambda ()
            (audit-phase (if final? "second typesetting pass" "first typesetting pass"))
            (unless (= (data-art-source-version) 3)
              (error "Loaded buffer is not XML v3"))
            (update-current-buffer)
            (update-forced)
            (if final?
                (exec-global (lambda () (audit-finish #t)))
                (begin
                  (audit-phase "waiting for asynchronous work")
                  ;; Headless views have no canvas endpoint for a repeating
                  ;; delayed command. Sleep only this actor, then enqueue a
                  ;; fresh pass through the headless-safe global dispatcher.
                  (usleep (* 1000 (string->number
                    (getenv "ATHENA_LOAD_AUDIT_SETTLE_MS"))))
                  (exec-global (lambda () (audit-typeset #t 100))))))
          audit-fail)))
    ;; exec-buffer uses try_submit: contention or a full command ring means
    ;; nothing was enqueued. Yield to the dispatcher before a bounded retry.
    (if (> attempts 1)
        (exec-global (lambda () (audit-typeset final? (- attempts 1))))
        (audit-finish '(could-not-enter-buffer-actor)))))

(catch #t
  (lambda ()
    (audit-phase "load Vault")
    (set-preference "check for updates" "off")
    (set-preference "document save mode" "manual")
    (let ((failure (vault-load-with-ns
                     (string->url audit-root) "Isolated load audit"
                     (getenv "ATHENA_LOAD_AUDIT_MAP")
                     (getenv "ATHENA_LOAD_AUDIT_NAMESPACES"))))
      (unless (equal? failure "") (error "Vault load failed" failure)))
    (audit-phase "load document")
    (when (buffer-load audit-source) (error "Document load failed" audit-source))
    (switch-to-buffer audit-source)
    (audit-typeset #f 100))
  audit-fail)
