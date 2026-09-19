;; Native generic document environment commands execute on the owning BufferActor.
(import-from (generic document-edit))
(init-style "generic")

(define (check condition message)
  (unless condition
    (error message (get-init-tree "page-medium")
                   (get-init-tree "page-orientation"))))

(init-default "page-medium" "page-orientation")
(check (test-default? "page-medium" "page-orientation")
       "multiple defaults initially absent from the init environment")
(check (test-default?) "empty default query is true")

(init-env "page-medium" "paper")
(check (not (test-default? "page-medium"))
       "explicit init value is not default")
(check (test-init? "page-medium" "paper") "native init equality")
(check (equal? (get-init-env "page-medium") "paper")
       "native atomic init lookup")

(init-env-tree "document-test-macro" '(macro "old"))
(check (equal? (get-init-env "document-test-macro") "old")
       "native macro init lookup")
(set-init-env "document-test-macro" "new")
(check (equal? (tree->stree (get-init-tree "document-test-macro"))
               '(macro "new"))
       "set-init-env preserves an existing macro wrapper")
(set-init-env "document-test-macro" '(macro "direct"))
(check (equal? (tree->stree (get-init-tree "document-test-macro"))
               '(macro "direct"))
       "set-init-env does not double-wrap macro input")

(init-env-tree "document-test-complex" '(concat "a" "b"))
(check (not (get-init-env "document-test-complex"))
       "non-atomic non-macro init has no string view")

(init-env "document-test-true" "true")
(check (test-init-true? "document-test-true") "native true init predicate")

(init-default "page-medium" "page-orientation")
(init-multi '("page-medium" "papyrus" "page-orientation" "landscape"))
(check (test-init? "page-medium" "papyrus") "init-multi sets first pair")
(check (test-init? "page-orientation" "landscape") "init-multi sets second pair")
(init-multi (list "page-medium" :default "page-orientation" :default))
(check (test-default? "page-medium" "page-orientation")
       "init-multi handles default keywords")

(init-env "page-medium" "paper")
(init-default "page-medium")
(check (test-default? "page-medium") "native init-default removes explicit value")

(init-env "page-medium" "paper")
(init-env "page-orientation" "landscape")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
