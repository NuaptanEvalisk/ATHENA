;; Run with QApplication and an actual actor-owned view. A headless test skips
;; system icon queries entirely and cannot detect GUI/actor wait cycles.
(import-from (generic generic-edit))
(init-style "generic")
(buffer-set-body (current-buffer)
  (stree->tree '(document
    (cardlink "Web" "https://example.invalid")
    (cardlink "Folder" "/tmp/")
    (cardlink "File" "/tmp/absent.ath"))))
(update-current-buffer)
(update-forced)
(for-each
  (lambda (entry)
    (unless (string? (system-icon-for-link (car entry) (cadr entry)))
      (error "GUI card link returned an invalid icon path" entry)))
  '(("https://example.invalid" "web")
    ("/tmp/" "folder")
    ("/tmp/absent.ath" "file")))
(update-forced)
