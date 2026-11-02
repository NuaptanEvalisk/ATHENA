;; Check real dispatch, not Scheme closure identity or procedure redefinition.
(native-keyboard-force #t)
(init-style "generic")

(define (check condition message)
  (unless condition (error "Native keymap regression" message
                           (cursor-path) (tree->stree (buffer-tree)))))
(define (reset text position)
  (selection-cancel)
  (buffer-set-body (current-buffer-url) (stree->tree `(document ,text)))
  (update-current-buffer)
  (tree-go-to (tree-ref (buffer-tree) 0) position)
  (update-current-buffer)
  (update-forced))

(reset "" 0)
(key-press "<")
(check (equal? (tree->stree (buffer-tree)) '(document "<"))
       "literal text binding remains distinct from a command")
(check (not (equal? (kbd-find-inv-binding '(kbd-left)) ""))
       "semantic inverse lookup remains available")

;; A user prefix overrides the shipped registry without creating a closure.
(set-user-shortcut "C-F12 x" "{\"op\":\"make\",\"tag\":\"strong\"}")
(reset "" 0)
(key-press "C-F12")
(key-press "x")
(check (equal? (tree->stree (buffer-tree)) '(document (strong "")))
       "native user shortcut resolves its multi-key prefix")
(remove-user-shortcut "C-F12 x")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
