;; Run on a real BufferActor. Link clicks invalidate the visited locus before
;; go-to-url, so navigation-only doubles cannot catch registry access here.
(init-style "generic")
(define fixture
  '(document
     (locus (id "visited-link-regression")
       (link "hyperlink" (id "visited-link-regression")
         (url "tmfs://wikilink/test-target"))
       "Wikilink")))
(buffer-set-body (current-buffer) (stree->tree fixture))
(update-current-buffer)
(update-forced)
(define loci (id->trees "visited-link-regression"))
(unless (pair? loci) (error "Test link locus was not registered"))
(declare-visited "id:visited-link-regression")
(for-each (lambda (t) (update-all-path (tree->path t))) loci)
(update-forced)
(unless (equal? (tree->stree (buffer-tree)) fixture)
  (error "Visited-link invalidation changed document contents"))
;; Repeated invalidation must remain inline, not fill the actor's own mailbox.
(do ((i 0 (+ i 1))) ((= i 64))
  (update-all-path (tree->path (buffer-tree))))
(update-forced)
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
