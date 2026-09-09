;; Run with evaluation-bar-test.py: -x executes in the owning BufferActor.
(import-from (athena athena tm-tools))
(init-style "generic")
(buffer-set-body (current-buffer) (stree->tree '(document "Refresh regression")))
(update-current-buffer)
(update-forced)

;; Remove Background calls picture-gc after successfully changing the PNG.
;; This previously traversed the GUI view registry on the calling actor.
(picture-gc)
(update-all-buffers)

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
