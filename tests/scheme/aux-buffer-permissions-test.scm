;; Run: guile --no-auto-compile tests/scheme/aux-buffer-permissions-test.scm ROOT
;; Evaluate the shipped permission handlers, not copies of their implementations.
(define root (cadr (command-line)))
(define == equal?)
(define-syntax with
  (syntax-rules () ((_ name value body ...) (let ((name value)) body ...))))
(define (tmfs-wrap name) #f)
(define (check expected actual label)
  (unless (equal? expected actual) (error label expected actual)))

(define aux-handler #f)
(define default-handler #f)
(call-with-input-file
  (string-append root "/ATHENA/progs/kernel/athena/tm-file-system.scm")
  (lambda (port)
    (let loop ((form (read port)))
      (unless (eof-object? form)
        (when (and (pair? form) (eq? (car form) 'tmfs-permission-handler)
                   (eq? (caadr form) 'aux))
          (set! aux-handler
            (eval `(lambda ,(cdadr form) ,@(cddr form)) (current-module))))
        (when (and (pair? form) (eq? (car form) 'tmfs-handler)
                   (eq? (cadr form) #t)
                   (equal? (caddr form) '(quote permission?)))
          (set! default-handler (eval (cadddr form) (current-module))))
        (loop (read port))))))

(check #t (procedure? aux-handler) "aux owns an explicit permission policy")
(check #t (procedure? default-handler) "default permission handler remains")
(for-each
  (lambda (name)
    (check #t (aux-handler name "read") "aux input is readable")
    (check #t (aux-handler name "write") "aux input is editable")
    (check #f (aux-handler name "execute") "no unrelated permission"))
  '("global-search" "search" "replace" "page-odd-header" "TeXmacs-input-1"))
(check #t (default-handler "unregistered/page" "read") "TMFS pages readable")
(check #f (default-handler "unregistered/page" "write") "TMFS pages read-only")
(display "PASS: auxiliary input permissions and read-only TMFS default\n")
