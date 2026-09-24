;; Native math keybindings must preserve physical prefix expansion and shared
;; generic-key precedence inside displayed mathematics.
(init-style "generic")

(define (body) (tree->stree (buffer-tree)))
(define (check condition message)
  (unless condition (error message (body) (cursor-path))))
(define (reset content)
  (selection-cancel)
  (buffer-set-body (current-buffer)
                   (stree->tree `(document (equation* ,content))))
  (update-current-buffer)
  (update-forced)
  (tree-go-to (tree-ref (buffer-tree) 0 0) :end)
  (commit-changes)
  (clear-undo-history))

;; Shared generic exact Space must win over the native "space var" prefix.
(reset "x")
(keyboard-press "space" 0)
(check (equal? (body) '(document (equation* "x ")))
       "math Space inserted the key name instead of a space")

;; Logical math/var prefixes must be expanded before the native registry is
;; frozen, so physical Alt+T and Tab reach the table variant family.
(reset "")
(keyboard-press "A-t" 0)
(check (equal? (body)
               '(document
                  (equation*
                    (tabular* (tformat (table (row (cell ""))))))))
       "Alt+T did not insert a math table")
(keyboard-press "tab" 0)
(check (equal? (body)
               '(document
                  (equation*
                    (matrix (tformat (table (row (cell ""))))))))
       "Tab did not advance the math table variant")

;; Ordinary symbol families use the same physical Tab expansion.
(reset "")
(keyboard-press "f" 0)
(keyboard-press "tab" 0)
(check (equal? (body) '(document (equation* "φ")))
       "Tab did not advance an ordinary math symbol variant")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
