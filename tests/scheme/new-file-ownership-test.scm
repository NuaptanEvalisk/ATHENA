;; Verify that creating a missing file keeps registry/open work global while
;; default style initialization runs on the new buffer's BufferActor.
(define root (cadr (command-line)))
(define in-global? #t)
(define in-buffer? #f)
(define created #f)
(define opened #f)
(define styled #f)
(define message #f)

(define (check condition message)
  (unless condition (error "New-file ownership regression" message)))

(define-macro (with var value . body)
  `(let ((,var ,value)) ,@body))

(define (require-global what)
  (check in-global? (string-append what " ran outside the global owner")))

(define (url-exists? name) #f)
(define (url-test? name mode) #t)
(define (url->system name) name)
(define (string->url name) name)
(define (url-rooted? name) #t)
(define (url-rooted-tmfs? name) #f)
(define (url-wrap name) #f)
(define (url-append a b) b)
(define (url-resolve a mode) a)
(define (url-pwd) "/tmp")
(define (url-format name) "texmacs")
(define (in? item xs) (memq item xs))
(define (nin? item xs) (not (memq item xs)))
(define :background ':background)

(define (buffer-exists? name) #f)
(define (buffer-set-body name body)
  (require-global "buffer-set-body")
  (set! created (list name body)))
(define (load-buffer-open name opts)
  (require-global "load-buffer-open")
  (set! opened (list name opts)))
(define (set-message left right)
  (require-global "set-message")
  (set! message (list left right)))
(define (buffer-set-default-style)
  (check in-buffer? "buffer-set-default-style ran outside the BufferActor")
  (set! styled #t))
(define (exec-buffer name thunk)
  (require-global "exec-buffer scheduling")
  (let ((old-global in-global?) (old-buffer in-buffer?))
    (dynamic-wind
      (lambda () (set! in-global? #f) (set! in-buffer? #t))
      thunk
      (lambda () (set! in-global? old-global) (set! in-buffer? old-buffer))))
  #t)

(define (load-definition wanted)
  (call-with-input-file
    (string-append root "/ATHENA/progs/athena/athena/tm-files.scm")
    (lambda (port)
      (let loop ((form (read port)))
        (cond ((eof-object? form)
               (error "New-file ownership regression"
                      "definition not found" wanted))
              ((and (pair? form)
                    (or (eq? (car form) 'define) (eq? (car form) 'tm-define))
                    (pair? (cdr form)) (pair? (cadr form))
                    (eq? (caadr form) wanted))
               (eval (if (eq? (car form) 'tm-define)
                         (cons 'define (cdr form)) form)
                     (current-module)))
              (else (loop (read port))))))))

(load-definition 'load-buffer-load)

(load-buffer-load "/tmp/athena-new-file-ownership.ath" '())

(check (equal? created
               '("/tmp/athena-new-file-ownership.ath" (document "")))
       "missing file did not create the expected document")
(check (equal? opened '("/tmp/athena-new-file-ownership.ath" ()))
       "new document was not opened globally")
(check styled "default style was not initialized on the BufferActor")
(check message "new-file status message was not emitted")

(display "PASS: missing-file default style initialization stays actor-owned\n")
