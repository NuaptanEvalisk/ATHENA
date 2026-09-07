;; Verify that the Visual Studio style buffer switcher leaves its source
;; BufferActor before consulting GUI-owned buffer/window state.
(define root (cadr (command-line)))
(define in-global? #f)
(define global-calls 0)
(define menu-calls 0)
(define choose-calls 0)
(define switch-calls 0)

(define (check condition message)
  (unless condition (error "Buffer switcher ownership regression" message)))

(define (!= a b) (not (equal? a b)))

(define (exec-global thunk)
  (set! global-calls (+ global-calls 1))
  (let ((previous in-global?))
    (dynamic-wind
      (lambda () (set! in-global? #t))
      thunk
      (lambda () (set! in-global? previous)))))

(define (buffer-menu-list limit)
  (check in-global? "buffer enumeration ran on a BufferActor")
  (set! menu-calls (+ menu-calls 1))
  '("one" "two"))

(define (buffer-switcher-entry name)
  (check in-global? "buffer metadata lookup ran on a BufferActor")
  (list name name name))

(define (visual-buffer-switcher-choose entries)
  (check in-global? "Qt chooser was entered before global dispatch")
  (set! choose-calls (+ choose-calls 1))
  "two")

(define (string->url value) value)

(define (switch-to-buffer* name)
  (check in-global? "buffer switch ran on a BufferActor")
  (set! switch-calls (+ switch-calls 1))
  (check (equal? name "two") "switcher selected the wrong buffer"))

(call-with-input-file
  (string-append root "/ATHENA/progs/athena/menus/file-menu.scm")
  (lambda (port)
    (let loop ((form (read port)))
      (cond ((eof-object? form)
             (error "Buffer switcher ownership regression"
                    "visual-buffer-switcher-show definition not found"))
            ((and (pair? form) (eq? (car form) 'tm-define)
                  (pair? (cdr form)) (pair? (cadr form))
                  (eq? (caadr form) 'visual-buffer-switcher-show))
             (eval (cons 'define (cdr form)) (current-module)))
            (else (loop (read port)))))))

;; Simulate invocation by a source-bound keyboard command on a BufferActor.
(visual-buffer-switcher-show)

(check (= global-calls 1) "switcher did not cross through exec-global exactly once")
(check (= menu-calls 1) "buffer list was not collected exactly once")
(check (= choose-calls 1) "visual chooser was not invoked exactly once")
(check (= switch-calls 1) "selected buffer was not switched exactly once")

(display "PASS: visual buffer switcher stays on the global owner\n")
