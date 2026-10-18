;; QCoreApplication has no native theme provider or QPixmap backend.
;; Returning an empty path selects the existing semantic card-link icon.
(for-each
  (lambda (entry)
    (unless (equal? (system-icon-for-link (car entry) (cadr entry)) "")
      (error "Headless card link unexpectedly requested a GUI icon" entry)))
  '(("https://example.invalid" "web")
    ("/tmp/" "folder")
    ("/tmp/absent.ath" "file")))
