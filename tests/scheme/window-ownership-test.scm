(use-modules (srfi srfi-1))
(define (check ok message)
  (unless ok (error "Window ownership regression" message)))

;; All of these queries run on one BufferActor, including queries about the
;; other buffer.  They verify that window/buffer ownership and reverse mappings
;; remain actor-safe without relying on any Scheme menu expansion.
(define windows
  (filter (lambda (win)
            (string-prefix? "Window " (buffer-get-title (window-to-buffer win))))
          (window-list)))
(unless (= (length windows) 2)
  (error "two fixture windows were not found"
    (map (lambda (win)
           (let ((buf (window-to-buffer win)))
             (list (url->string win) (url->string buf) (buffer-get-title buf))))
         (window-list))))
(define buffers (map window-to-buffer windows))
(check (= (length (delete-duplicates buffers)) 2)
       "window mappings did not retain distinct buffers")
(for-each
  (lambda (win name)
    (check (not (url-none? name)) "window lost its buffer")
    (check (member win (buffer->windows name)) "reverse window mapping failed")
    (check (number? (buffer-last-visited name)) "visit metadata unavailable")
    (check (string-prefix? "Window " (buffer-get-title name)) "title unavailable")
    (check (boolean? (buffer-menu-modified? name)) "unsaved marker unavailable"))
  windows buffers)
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file
  (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
