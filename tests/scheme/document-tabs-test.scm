;; Tab ownership is visible to actors through the published window catalog.
(define windows (window-list))
(define buffers (map window-to-buffer windows))
(unless (= (length windows) (length (list-remove-duplicates buffers)))
  (error "Document tab regression" "duplicate buffer tabs"))
(for-each
  (lambda (win buf)
    (unless (equal? (buffer->windows buf) (list win))
      (error "Document tab regression" "buffer/tab publication mismatch")))
  windows buffers)
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (system->url (string-append (getenv "HOME") "/evaluation.pdf")))
