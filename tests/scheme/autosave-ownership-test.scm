;; Verify that periodic autosave orchestration returns to the global/UI owner.
(define root (cadr (command-line)))
(define scheduled #f)
(define global-calls 0)
(define autosave-calls 0)
(define in-global? #f)

(define (check condition message)
  (unless condition (error "Autosave ownership regression" message)))

(define (get-preference key)
  (if (equal? key "autosave") "1" ""))

(define-macro (delayed timing . body)
  `(set! scheduled (lambda () ,@body)))

(define (exec-global thunk)
  (set! global-calls (+ global-calls 1))
  (let ((previous in-global?))
    (dynamic-wind
      (lambda () (set! in-global? #t))
      thunk
      (lambda () (set! in-global? previous)))))

(define (autosave-now)
  (check in-global? "autosave-all must not run on a BufferActor")
  (set! autosave-calls (+ autosave-calls 1)))

(call-with-input-file
  (string-append root "/ATHENA/progs/athena/athena/tm-files.scm")
  (lambda (port)
    (let loop ((form (read port)))
      (cond ((eof-object? form)
             (error "Autosave ownership regression"
                    "autosave-delayed definition not found"))
            ((and (pair? form) (eq? (car form) 'tm-define)
                  (pair? (cdr form)) (pair? (cadr form))
                  (eq? (caadr form) 'autosave-delayed))
             (eval (cons 'define (cdr form)) (current-module)))
            (else (loop (read port)))))))

(autosave-delayed)
(check (procedure? scheduled) "autosave timer was not scheduled")
(check (= global-calls 0) "autosave ran before its timer fired")
(check (= autosave-calls 0) "autosave ran before its timer fired")
(scheduled)
(check (= global-calls 1) "autosave timer did not cross through exec-global")
(check (= autosave-calls 1) "autosave-now did not run exactly once")

(display "PASS: delayed autosave orchestration runs on the global owner\n")
