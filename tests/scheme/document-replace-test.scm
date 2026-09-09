;; Isolated native replacement checks, run with evaluation-bar-test.py.
(init-style "generic")

(define (check condition message)
  (unless condition
    (error "Native document replacement" message
           (tree->stree (buffer-tree)))))

(define (search-text text ignore-case?)
  (document-search (stree->tree text) ignore-case?))
(define (replace-text text all?)
  (document-replace (stree->tree text) all?))

(define (body) (tree->stree (buffer-tree)))
(define (reset-document t)
  (document-search-clear)
  (buffer-set-body (current-buffer) (stree->tree t))
  (update-current-buffer)
  (update-forced)
  (tree-go-to (buffer-tree) :start)
  (commit-changes)
  (clear-undo-history))

(for-each (lambda (name) (check (not (defined? name)) name))
          '(interactive-replace replace-toolbar open-replace replace-start
            key-press-replace with-linking-tool? make-link))

(reset-document '(document "cat Cat cat"))
(check (= (search-text "cat" #f) 2) "match case")
(check (= (replace-text "dog" #f) 1) "replace current")
(check (equal? (body) '(document "dog Cat cat")) "only current replaced")
(check (= (replace-text "dog" #f) 1) "advance after replacement")
(check (equal? (body) '(document "dog Cat dog")) "next match replaced")

(reset-document '(document "cat Cat cat"))
(search-text "cat" #f)
(document-search-navigate #f #t)
(document-search-navigate #f #f)
(check (= (replace-text "dog" #f) 1) "replace after wrapped navigation")
(check (equal? (body) '(document "cat Cat dog")) "last match selected")

(reset-document '(document "cat Cat cat"))
(check (= (search-text "cat" #t) 3) "ignore case")
(check (= (replace-text "catcat" #t) 3) "finite original match set")
(check (equal? (body) '(document "catcat catcat catcat")) "replace all")
(commit-changes)
(undo 0)
(check (equal? (body) '(document "cat Cat cat")) "single undo restores all")
(redo 0)
(check (equal? (body) '(document "catcat catcat catcat")) "redo all")

(reset-document '(document "aaaa"))
(check (= (search-text "aa" #f) 2) "nonoverlapping matches")
(check (= (replace-text "" #t) 2) "empty replacement deletes")
(check (equal? (body) '(document "")) "all text removed")
(check (= (search-text "" #f) 0) "empty query")
(check (= (replace-text "x" #t) 0) "empty query cannot insert")

(reset-document '(document (concat "cat " (strong "cat") " cat")))
(check (= (search-text "cat" #f) 3) "matches in formatting")
(check (= (replace-text "dog" #t) 3) "replace through formatting")
(check (equal? (body) '(document (concat "dog " (strong "dog") " dog")))
       "formatting retained")

;; Editing while the bar is open must not leave usable stale match paths.
(reset-document '(document "cat cat"))
(search-text "cat" #f)
(selection-cancel)
(tree-go-to (buffer-tree) :start)
(insert "prefix ")
(check (= (replace-text "dog" #t) 2) "refresh before modification")
(check (equal? (body) '(document "prefix dog dog")) "no stale offsets")

(let ((word (utf8->cork (string #\x3bb)))
      (replacement (utf8->cork (string #\x3b1))))
  (reset-document `(document ,(string-append word " " word)))
  (check (= (search-text word #f) 2) "non-ASCII matching")
  (check (= (replace-text replacement #t) 2) "non-ASCII replacement")
  (check (equal? (body) `(document ,(string-append replacement " " replacement)))
         "internal character encoding retained"))

(document-search-clear)
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
