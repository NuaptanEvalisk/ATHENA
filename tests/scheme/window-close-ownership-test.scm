;; Verify that document-window teardown never runs in a BufferActor domain.
(define root (cadr (command-line)))
(define in-global? #f)
(define global-calls 0)
(define pending-global #f)
(define pending-delayed #f)
(define killed-window #f)
(define closed-buffer #f)

(define (check condition message)
  (unless condition (error "Window close ownership regression" message)))

(define (require-global what)
  (check in-global? (string-append what " ran outside the global owner")))

(define (current-buffer) "buffer-a")
(define (current-window) "window-a")
(define (buffer-embedded? buf) #f)
(define (url->string value) value)
(define (string->url value) value)
(define (url-none? value) (equal? value "none"))
(define (window->buffer win)
  (require-global "window->buffer")
  (if (equal? win "window-b") "buffer-b" "buffer-a"))
(define (windows-number) (require-global "windows-number") 2)
(define (ads-open-panes?) (require-global "ads-open-panes?") #f)
(define (buffer-needs-save-confirmation? buf)
  (require-global "buffer-needs-save-confirmation?")
  #f)
(define (kill-window win)
  (require-global "kill-window")
  (set! killed-window win))
(define (buffer-close buf)
  (require-global "buffer-close")
  (set! closed-buffer buf))
(define (safely-quit-ATHENA)
  (error "Window close ownership regression" "unexpected quit path"))
(define (user-confirm . args)
  (error "Window close ownership regression" "unexpected confirmation"))
(define (alt-window-search . args) #f)
(define (alt-windows-delete . args) #f)

(define-macro (delayed timing . body)
  `(set! pending-delayed (lambda () ,@body)))

(define (exec-global thunk)
  (set! global-calls (+ global-calls 1))
  (if in-global?
      (thunk)
      (set! pending-global thunk)))

(define wanted
  '(close-buffer-after-window
    close-buffer-after-window-later
    do-kill-window-global
    safely-kill-window-global
    safely-kill-window))

(define (definition-name form)
  (and (pair? form)
       (memq (car form) '(define tm-define))
       (pair? (cdr form))
       (pair? (cadr form))
       (caadr form)))

(call-with-input-file
  (string-append root "/ATHENA/progs/athena/athena/tm-server.scm")
  (lambda (port)
    (let loop ((form (read port)) (remaining wanted))
      (cond ((null? remaining) #t)
            ((eof-object? form)
             (error "Window close ownership regression"
                    "required close definition not found" remaining))
            ((memq (definition-name form) remaining)
             (eval (if (eq? (car form) 'tm-define)
                       (cons 'define (cdr form))
                       form)
                   (current-module))
             (loop (read port)
                   (delq (definition-name form) remaining)))
            (else (loop (read port) remaining))))))

(define (run-global-pending)
  (check (procedure? pending-global) "global close continuation was not queued")
  (let ((thunk pending-global))
    (set! pending-global #f)
    (let ((previous in-global?))
      (dynamic-wind
        (lambda () (set! in-global? #t))
        thunk
        (lambda () (set! in-global? previous))))))

;; Source-bound close: only current-buffer/current-window and string conversion
;; may happen before exec-global. All window/ADS lookup and teardown is global.
(safely-kill-window)
(check (= global-calls 1) "close did not cross through exec-global exactly once")
(check (not killed-window) "window was killed from the BufferActor")
(run-global-pending)
(check (equal? killed-window "window-a") "wrong current window was closed")
(check (procedure? pending-delayed) "post-close cleanup was not scheduled")

;; The delayed cleanup is allowed to wake in an actor domain, but it must hand
;; the actual buffer-registry operation back to the global owner.
(set! pending-global #f)
(pending-delayed)
(check (procedure? pending-global) "cleanup did not cross through exec-global")
(check (not closed-buffer) "buffer cleanup ran in the BufferActor")
(run-global-pending)
(check (equal? closed-buffer "buffer-a") "wrong buffer was cleaned up")

;; Qt's close command now supplies an inert Scheme string, not a native URL.
(set! pending-global #f)
(set! pending-delayed #f)
(set! killed-window #f)
(safely-kill-window "window-b")
(run-global-pending)
(check (equal? killed-window "window-b") "explicit window string was not honored")

(display "PASS: window close lookup, ADS state and teardown stay global\n")
