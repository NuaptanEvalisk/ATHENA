(define (check ok message)
  (unless ok (error "Window menu ownership regression" message)))

;; All of these queries run on one BufferActor, including queries about the
;; other buffer. Merely evaluating workspace-menu does not expand its links.
(define windows
  (list-filter (window-list)
    (lambda (win)
      (string-starts? (buffer-get-title (window-to-buffer win)) "Menu "))))
(unless (= (length windows) 2)
  (error "two fixture windows were not found"
    (map (lambda (win)
           (let ((buf (window-to-buffer win)))
             (list (url->string win) (url->string buf) (buffer-get-title buf))))
         (window-list))))
(define buffers (map window-to-buffer windows))
(check (= (length (list-remove-duplicates buffers)) 2)
       "window mappings did not retain distinct buffers")
(for-each
  (lambda (win name)
    (check (not (url-none? name)) "window lost its buffer")
    (check (member win (buffer->windows name)) "reverse window mapping failed")
    (check (number? (buffer-last-visited name)) "visit metadata unavailable")
    (check (string-starts? (buffer-get-title name) "Menu ") "title unavailable")
    (check (boolean? (buffer-menu-modified? name)) "unsaved marker unavailable"))
  windows buffers)
(lazy-menu-force-all)
(menu-expand (workspace-menu))
(menu-expand (window-list-menu))
(menu-expand (go-menu))
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file
  (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
