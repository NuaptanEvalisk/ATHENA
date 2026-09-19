;; Native document preamble queries and actor-owned show/hide/toggle behavior.
(import-from (generic document-part))
(init-style "generic")

(define (check condition message . details)
  (unless condition
    (apply error (cons message details))))

(define hidden-doc
  (stree->tree '(document (hide-preamble (document "defs")) "body")))
(check (document-has-preamble? hidden-doc)
       "native detached preamble predicate recognizes hidden preamble")
(check (equal? (tree->stree (document-get-preamble hidden-doc))
               '(document "defs"))
       "native detached preamble getter returns preamble body")
(check (not (document-has-preamble? (stree->tree '(document "body"))))
       "native detached preamble predicate rejects ordinary document")
(check (equal? (tree->stree (document-get-preamble (stree->tree '(document "body"))))
               '(document ""))
       "native detached preamble getter returns empty fallback")

(buffer-set-body
  (current-buffer)
  (stree->tree '(document "body")))
(update-current-buffer)
(check (not (buffer-has-preamble?))
       "plain active document starts without preamble")
(buffer-make-preamble)
(check (buffer-has-preamble?)
       "native preamble creation adds preamble")
(check (in-preamble-mode?)
       "native preamble creation enters shown preamble mode")
(check (equal? (tree->stree (buffer-tree))
               '(document
                  (show-preamble (document ""))
                  (ignore (document "body"))))
       "native preamble creation preserves main document body")
(check (equal? (tree->stree (buffer-get-preamble)) '(document ""))
       "native buffer preamble getter sees active preamble")

(toggle-preamble-mode)
(check (not (in-preamble-mode?))
       "native toggle hides shown preamble")
(check (equal? (tree->stree (buffer-tree))
               '(document (hide-preamble (document "")) "body"))
       "native hide transform restores main document shape")
(toggle-preamble-mode)
(check (in-preamble-mode?)
       "native toggle re-shows hidden preamble")

(buffer-hide-preamble)
(check (equal? (tree->stree (buffer-tree))
               '(document (hide-preamble (document "")) "body"))
       "native direct hide helper reproduces hidden shape")
(buffer-show-preamble)
(check (equal? (tree->stree (buffer-tree))
               '(document
                  (show-preamble (document ""))
                  (ignore (document "body"))))
       "native direct show helper reproduces shown shape")

;; Generic kbd-remove keeps its Scheme multimethod ownership, but delegates
;; the show->hide transition through the native helper before removing the
;; empty preamble node.
(kbd-remove (tree-ref (buffer-tree) 0) #t)
(check (equal? (tree->stree (buffer-tree)) '(document "body"))
       "generic kbd-remove removes empty shown preamble through native helper")
(check (not (buffer-has-preamble?))
       "kbd-remove leaves document without preamble")

;; Existing hidden preamble with multiple body children round-trips exactly.
(buffer-set-body
  (current-buffer)
  (stree->tree
    '(document
       (hide-preamble (document "defs"))
       "alpha"
       (concat "beta" "gamma"))))
(update-current-buffer)
(toggle-preamble-mode)
(check (equal? (tree->stree (buffer-tree))
               '(document
                  (show-preamble (document "defs"))
                  (ignore (document "alpha" (concat "beta" "gamma")))))
       "native show transform wraps all main document children")
(toggle-preamble-mode)
(check (equal? (tree->stree (buffer-tree))
               '(document
                  (hide-preamble (document "defs"))
                  "alpha"
                  (concat "beta" "gamma")))
       "native hide transform restores all main document children")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
