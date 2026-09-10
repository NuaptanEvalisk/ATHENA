;; Run in global context in the isolated runtime provided by the Python driver.
(define test-root (getenv "ATHENA_SAVE_AS_TEST_ROOT"))
(define old-name (string->url (string-append test-root "/old.ath")))
(define new-name (string->url (string-append test-root "/new.ath")))
(define result-file (string-append test-root "/result.scm"))

(buffer-set old-name
  '(document (TeXmacs "2.1.4") (style "generic")
     (body (document "SAVE-AS-OWNERSHIP"))))
(switch-to-buffer old-name)

(unless
  (exec-buffer old-name
    (lambda ()
      (save-buffer-as-main new-name old-name (list :overwrite))
      (unless (equal? (url->system (current-buffer)) (url->system new-name))
        (error "Save As did not synchronously rename actor state"))
      (exec-global
        (lambda ()
          ;; The rename effect was published before this global callback. Wait
          ;; for the next UI pass so the GUI-owned registry has consumed it.
          (delayed (:pause 500)
            (call-with-output-file result-file
              (lambda (port)
                (write
                  (list 'old-exists (buffer-exists? old-name)
                        'new-exists (buffer-exists? new-name)
                        'saved
                        (and (url-exists? new-name)
                             (string-contains? (string-load new-name)
                                               "SAVE-AS-OWNERSHIP"))
                        'title (buffer-get-title new-name))
                  port))))))))
  (error "Could not dispatch Save As actor command"))
