;; Run in global context in the isolated runtime provided by the Python driver.
(define test-root (getenv "ATHENA_SAVE_TEST_ROOT"))
(define save-mode (getenv "ATHENA_SAVE_TEST_MODE"))
(define first-name (string->url (string-append test-root "/first.ath")))
(define second-name (string->url (string-append test-root "/second.ath")))
(define completed-count 0)
(define failed? #f)

(define (report tag value)
  (call-with-output-file (string-append test-root "/" tag ".result")
    (lambda (port) (write value port))))
(define (finished tag result)
  (exec-global
    (lambda ()
      (report tag result)
      (set! failed? (or failed? (not (eq? result #t))))
      (set! completed-count (+ completed-count 1))
      (when (= completed-count 2) (exit (if failed? 1 0))))))

(define (save-round name tag round)
  (catch #t
    (lambda ()
      (unless (equal? (url->system (current-buffer)) (url->system name))
        (error "Save ran on the wrong buffer" tag))
      (if (= round 3) (finished tag #t)
          (begin
            (buffer-pretend-modified name)
            (if (equal? save-mode "plain")
                (begin
                  (when (buffer-save name) (error "Native save failed" tag))
                  (save-round name tag (+ round 1)))
                (save-buffer-manual name
                  (cons 'on-saved
                    (lambda () (save-round name tag (+ round 1)))))))))
    (lambda args (finished tag args))))

(define (fixture text other)
  `(document (TeXmacs "2.1.4") (style "generic")
     (body (document
       (section "Save regression")
       (definition (document (with "font-series" "bold" ,text)
                             "Definition body."))
       (hlink "Other document" ,(url->system other))))))

(set-preference "vault auto anchor enunciations on save" "on")
(set-preference "vault auto approve anchor changes"
                (if (equal? save-mode "manual-approve") "on" "off"))
(buffer-set first-name (fixture "FIRST BUFFER" second-name))
(switch-to-buffer first-name)
(buffer-set second-name (fixture "SECOND BUFFER" first-name))
(switch-to-buffer second-name)
;; The first actor saves in the background while the second view is current.
(unless (exec-buffer first-name (lambda () (save-round first-name "first" 0)))
  (error "Could not dispatch first save"))
(unless (exec-buffer second-name (lambda () (save-round second-name "second" 0)))
  (error "Could not dispatch second save"))
