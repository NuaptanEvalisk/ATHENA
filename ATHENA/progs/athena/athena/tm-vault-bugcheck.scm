(texmacs-module (athena athena tm-vault-bugcheck)
  (:use (kernel boot abbrevs)
        (kernel library list)
        (link ref-edit)))

(define vault-bugcheck-run-id 0)
(define vault-bugcheck-errors '())
(define vault-bugcheck-load-timeout 120)
(define vault-bugcheck-settle-ticks 20)

(define (vault-bugcheck-root-base)
  (url-append (vault-get-root) ""))

(define (vault-bugcheck-wrap-line line width)
  (let loop ((s line) (wrapped '()))
    (if (<= (string-length s) width)
        (reverse (cons s wrapped))
        (loop (substring s width (string-length s))
              (cons (substring s 0 width) wrapped)))))

(define (vault-bugcheck-wrap-text text)
  (let loop ((lines (string-decompose text "\n")) (wrapped '()))
    (if (null? lines)
        (if (null? wrapped)
            ""
            (apply string-append (list-intersperse (reverse wrapped) "\n")))
        (loop (cdr lines)
              (append (reverse (vault-bugcheck-wrap-line (car lines) 96))
                      wrapped)))))

(define (vault-bugcheck-add-error rel kind detail)
  (let ((entry (string-append rel "\n  " kind ": " detail)))
    (set! vault-bugcheck-errors
          (append vault-bugcheck-errors (list entry)))))

(define (vault-bugcheck-numbered-errors errors)
  (let loop ((todo errors) (index 1) (out '()))
    (if (null? todo)
        (reverse out)
        (loop (cdr todo) (+ index 1)
              (cons (string-append (number->string index) ". " (car todo))
                    out)))))

(define (vault-bugcheck-summary total)
  (let* ((count (length vault-bugcheck-errors))
         (header (if (== count 0)
                     (string-append "Vault bugcheck completed.\n"
                                    (number->string total)
                                    " .ath files checked.\nNo errors found.")
                     (string-append "Vault bugcheck completed.\n"
                                    (number->string total)
                                    " .ath files checked.\n"
                                    (number->string count)
                                    " error(s) found:\n\n")))
         (body (if (== count 0)
                   ""
                   (apply string-append
                          (list-intersperse
                           (vault-bugcheck-numbered-errors vault-bugcheck-errors)
                           "\n\n")))))
    (string-append header body)))

(define (vault-bugcheck-log-summary summary)
  (catch #t
    (lambda ()
      (string-save summary
                   (url-append (vault-get-root)
                               (unix->url "VaultBugcheck.log"))))
    (lambda (key . args)
      (display* "Vault bugcheck could not write log: "
                key ", " args "\n"))))

(tm-widget ((vault-bugcheck-report-widget msg) done)
  (padded
    (resize '("480px" "760px" "1100px") '("260px" "520px" "760px")
      (scrollable
        (for (line (string-decompose msg "\n"))
          (hlist // (text line) >>)))
      ===
      (bottom-buttons >> ("Ok" (done))))))

(define (vault-bugcheck-show-summary total)
  (let ((summary (vault-bugcheck-summary total)))
    (display* summary "\n")
    (vault-bugcheck-log-summary summary)
    (dialogue-window
     (vault-bugcheck-report-widget (vault-bugcheck-wrap-text summary))
     noop
     "Vault Bugcheck")))

(define (vault-ath-files-recursive dir)
  (let* ((files (url-read-directory dir "*.ath"))
         (subdirs (list-sort
                   (list-filter (url-read-directory dir "*") url-directory?)
                   (lambda (a b) (string<=? (url->unix a) (url->unix b)))))
         (res files))
    (for (d subdirs)
      (let ((name (url->unix (url-tail d))))
        (if (not (string-starts? name "."))
            (set! res (append res (vault-ath-files-recursive d))))))
    (list-sort res (lambda (a b) (string<=? (url->unix a) (url->unix b))))))

(define (vault-bugcheck-rel u)
  (url->unix (url-delta (vault-bugcheck-root-base) u)))

(define (vault-bugcheck-report-error u key args)
  (let ((rel (vault-bugcheck-rel u)))
    (display* "Vault bugcheck failed while loading " rel ": "
              key ", " args "\n")
    (vault-bugcheck-add-error
     rel "load error" (object->string (cons key args)))))

(define (vault-bugcheck-load-file u)
  (catch #t
    (lambda ()
      (clear-debug-messages)
      (load-buffer u)
      #t)
    (lambda (key . args)
      (vault-bugcheck-report-error u key args)
      #f)))

(define (vault-bugcheck-debug-messages)
  (tree-children (get-debug-messages "Error messages" 1000)))

(define (vault-bugcheck-format-debug-message m)
  (let ((channel (tree->string (tree-ref m 0)))
        (message (tree->string (tree-ref m 1))))
    (string-append channel ": " message)))

(define (vault-bugcheck-current-file? u)
  (and (current-buffer)
       (== (current-buffer-url) u)))

(define (vault-bugcheck-force-typeset u)
  (catch #t
    (lambda ()
      (if (vault-bugcheck-current-file? u)
          (update-forced))
      #t)
    (lambda (key . args)
      (vault-bugcheck-report-error u key args)
      #f)))

(define (vault-bugcheck-after-load files total index run-id u)
  (when (== run-id vault-bugcheck-run-id)
    (catch #t
      (lambda ()
        (let ((rel (vault-bugcheck-rel u)))
          (vault-bugcheck-force-typeset u)
          (let ((messages (vault-bugcheck-debug-messages))
                (dups (duplicate-labels)))
            (for (m messages)
              (let ((message (vault-bugcheck-format-debug-message m)))
                (display* "Vault bugcheck found TeXmacs diagnostic in "
                          rel ": " message "\n")
                (vault-bugcheck-add-error rel "TeXmacs diagnostic" message)))
            (when dups
              (display* "Vault bugcheck found duplicate labels in " rel "\n")
              (vault-bugcheck-add-error
               rel "duplicate labels"
               (string-append (number->string (length dups))
                              " duplicate label occurrence(s)"))))
          (vault-bugcheck-step (cdr files) total (+ index 1) run-id)))
      (lambda (key . args)
        (vault-bugcheck-report-error u key args)
        (vault-bugcheck-step (cdr files) total (+ index 1) run-id)))))

(define (vault-bugcheck-wait-loaded files total index run-id u attempts-left)
  (when (== run-id vault-bugcheck-run-id)
    (cond
      ((vault-bugcheck-current-file? u)
       (vault-bugcheck-settle files total index run-id u
                              vault-bugcheck-settle-ticks))
      ((<= attempts-left 0)
       (vault-bugcheck-report-error
        u 'timeout
        (list "Timed out waiting for file to become the current buffer"))
       (vault-bugcheck-step (cdr files) total (+ index 1) run-id))
      (else
       (delayed (:pause 100)
         (vault-bugcheck-wait-loaded files total index run-id u
                                     (- attempts-left 1)))))))

(define (vault-bugcheck-settle files total index run-id u ticks-left)
  (when (== run-id vault-bugcheck-run-id)
    (cond
      ((not (vault-bugcheck-current-file? u))
       (vault-bugcheck-wait-loaded files total index run-id u
                                   vault-bugcheck-load-timeout))
      ((<= ticks-left 0)
       (vault-bugcheck-after-load files total index run-id u))
      (else
       (delayed (:pause 100)
         (vault-bugcheck-settle files total index run-id u
                                (- ticks-left 1)))))))

(define (vault-bugcheck-step files total index run-id)
  (when (== run-id vault-bugcheck-run-id)
    (if (null? files)
        (begin
          (display* "Vault bugcheck completed: " total " files checked\n")
          (vault-bugcheck-show-summary total))
        (let* ((u (car files))
               (rel (vault-bugcheck-rel u))
               (msg (string-append "Checking "
                                   (number->string index) "/"
                                   (number->string total) ": " rel)))
          (display* "Vault bugcheck: " msg "\n")
          (set-message msg "Vault Bugcheck")
          (if (vault-bugcheck-load-file u)
              (vault-bugcheck-wait-loaded files total index run-id u
                                          vault-bugcheck-load-timeout)
              (vault-bugcheck-step (cdr files) total (+ index 1) run-id))))))

(tm-define (vault-bugcheck)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Vault Bugcheck")
      (let ((files (vault-ath-files-recursive (vault-get-root))))
        (set! vault-bugcheck-run-id (+ vault-bugcheck-run-id 1))
        (if (null? files)
            (show-message "No .ath files found in the current vault." "Vault Bugcheck")
            (begin
              (clear-debug-messages)
              (set! vault-bugcheck-errors '())
              (display* "Vault bugcheck starting with "
                        (length files) " files\n")
              (vault-bugcheck-step files (length files) 1
                                   vault-bugcheck-run-id))))))
