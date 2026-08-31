;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-global-transformation.scm
;; DESCRIPTION : Run user-provided Scheme transformations over a Vault
;; COPYRIGHT   : (C) 2026  Nuaptan Felix Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-global-transformation))

;; A transformation script registers exactly one procedure.  The procedure is
;; called with a Vault-relative path and a complete ATHENA document tree.  It
;; returns #f when unchanged, or the complete replacement document tree.
(define global-transformation-registration #f)

(define-public (register-global-transformation name transformer)
  (cond (global-transformation-registration
         (error "A transformation script must register exactly one transformer"))
        ((and (string? name) (not (string-null? name))
              (procedure? transformer))
         (set! global-transformation-registration (cons name transformer)))
        (else
         (error "register-global-transformation expects a non-empty name and a procedure"))))

(define (global-transformation-error-message key arguments)
  (call-with-output-string
    (lambda (port)
      (write key port)
      (display ": " port)
      (write arguments port))))

(define (global-transformation-result? result)
  (and (tree? result) (tree-func? result 'tuple) (>= (tree-arity result) 4)))

(define (global-transformation-report result)
  (if (not (global-transformation-result? result))
      (show-message "The transformation runner returned an invalid result"
                    "Global transformation")
      (let ((status (tree->string (tree-ref result 0)))
            (message (tree->string (tree-ref result 3))))
        (cond ((== status "error")
               (set-message message "Global transformation error"))
              ((== status "cancelled")
               (set-message message "Global transformation"))
              (else
               (set-message message "Global transformation"))))))

(define (global-transformation-load-script script)
  (if (!= (locase-all (url-suffix script)) "scm")
      (show-message "Select a Scheme (.scm) transformation script"
                    "Global transformation")
      (begin
        (set! global-transformation-registration #f)
        (with loaded?
            (catch #t
              (lambda ()
                (save-module-excursion
                  (lambda ()
                    (set-current-module
                      (resolve-module '(athena athena tm-global-transformation)))
                    (primitive-load (url->system script))))
                #t)
              (lambda (key . arguments)
                (show-message
                  (string-append
                    "Could not load the selected Scheme transformation script\n\n"
                    (global-transformation-error-message key arguments))
                  "Global transformation")
                #f))
          (when loaded?
            (if (not global-transformation-registration)
                (show-message
                  "The script did not call register-global-transformation"
                  "Global transformation")
                (let ((name (car global-transformation-registration))
                      (transformer (cdr global-transformation-registration)))
                  (global-transformation-report
                    (global-transformation-run transformer name)))))))))

(tm-define (run-global-transformation)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "Load a Vault before running a global transformation"
                    "Global transformation")
      (let* ((dependencies (url-append (vault-get-root) "dependencies"))
             (initial (if (url-exists? dependencies)
                          dependencies (vault-get-root))))
        (choose-file global-transformation-load-script
                     "Run global transformation" "scheme"
                     "" initial))))
