;; Native live-spell current-range policy reads the owning editor's alternate
;; selection directly; no asynchronous spell traversal is needed for this test.
(import-from (generic live-spell))
(init-style "generic")
(init-env "language" "english")

(define (check condition message . details)
  (unless condition
    (apply error (cons message details))))

(buffer-set-body
  (current-buffer)
  (stree->tree '(document "hello wurld bye")))
(update-current-buffer)

(define text-node (tree-ref (buffer-tree) 0))
(tree-go-to text-node 6)
(define word-start (cursor-path))
(tree-go-to text-node 11)
(define word-end (cursor-path))
(set-alt-selection "spell-live" (list word-start word-end))
(tree-go-to text-node 8)

(check (equal? (spell-live-current-selection) (list word-start word-end))
       "native live spell selection finds range containing cursor")
(check (equal? (spell-live-current-word) "wurld")
       "native live spell word extracts selected atomic text")
(check (equal? (spell-live-current-language) "english")
       "native live spell language resolves editor environment at range")
(let ((suggestions (spell-live-current-suggestions)))
  (check (list? suggestions)
         "native live spell suggestions returns a Scheme list")
  (check (<= (length suggestions) 9)
         "native live spell suggestions preserves nine-item cap"))

(tree-go-to text-node 0)
(check (not (spell-live-current-selection))
       "native live spell selection rejects cursor outside error range")
(check (not (spell-live-current-word))
       "native live spell word rejects cursor outside error range")
(check (not (spell-live-current-language))
       "native live spell language rejects cursor outside error range")
(check (null? (spell-live-current-suggestions))
       "native live spell suggestions is empty outside error range")

(cancel-alt-selection "spell-live")
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
