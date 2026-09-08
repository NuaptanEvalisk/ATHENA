(import-from (text text-edit) (generic generic-edit)
             (table table-edit) (athena keyboard latex-kbd))
(init-style "generic")

(define (check condition message)
  (unless condition (error "Proof equation array navigation" message
                           (tree->stree (buffer-tree)) (cursor-path))))

(define (reset-proof following?)
  (selection-cancel)
  (buffer-set-body (current-buffer)
    (stree->tree
      `(document (proof (document "something" "" ,@(if following? '("following") '())))
                 "outside")))
  (update-current-buffer)
  (update-forced)
  (tree-go-to (buffer-tree) 0 0 1 :start))

(define (insert-array spelling)
  (make-hybrid)
  (if (equal? spelling "{")
      (hybrid-kbd-curly-left)
      (begin (insert spelling) (activate-hybrid #f)))
  (check (tree-innermost '(eqnarray eqnarray*)) "creation must retain formula focus"))

(for-each
  (lambda (spelling)
    (reset-proof #f)
    (insert-array spelling)
    (let ((body (tree-ref (buffer-tree) 0 0)))
      (check (= (tree-arity body) 3) "one trailing paragraph inside proof")
      (check (equal? (tree->stree (tree-ref body 2)) "") "trailing paragraph is empty"))
    (let ((array (tree-innermost '(eqnarray eqnarray*))))
      (tree-set! array 0
        '(document (tformat (table
           (row (cell "1") (cell "2") (cell "3"))
           (row (cell "4") (cell "5") (cell "6"))))))
      (update-current-buffer)
      (update-forced)
      (tree-go-to (tree-ref array 0 0 0 1 2 0) :end))
    ;; Headless execution does not run the GUI apply_changes cycle needed
    ;; by physical arrows. Check the editable source landing point instead.
    (tree-go-to (buffer-tree) 0 0 2 :start)
    (check (inside? 'proof) "landing point must remain in proof")
    (check (not (tree-innermost '(eqnarray eqnarray*))) "landing point must be outside array")
    (insert "continuation")
    (check (inside? 'proof) "continuation must belong to proof")
    (reset-proof #t)
    (insert-array spelling)
    (check (= (tree-arity (tree-ref (buffer-tree) 0 0)) 3)
           "do not add a paragraph when following text exists"))
  '("{" "eqnarray" "eqnarray*"))

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
