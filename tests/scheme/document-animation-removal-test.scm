;; Run with evaluation-bar-test.py to exercise the real owning BufferActor.
(import-from (generic generic-menu) (generic insert-menu)
             (dynamic fold-edit) (graphics graphics-edit) (graphics graphics-menu))
(init-style "generic")

(define (check condition message)
  (unless condition
    (error "Document animation removal" message (tree->stree (buffer-tree)))))

(for-each
  (lambda (name)
    (check (not (defined? name)) (symbol->string name)))
  '(players-set-elapsed players-set-speed update-players
    animate-checkout animate-commit anim-control-times make-sound make-animation))
(check (not (member "anim-id" (graphics-all-attributes)))
       "graphics must not publish animation attributes")
(check (equal? (stree-radical '(with "color" "red" (point "0" "0")))
               '(point "0" "0"))
       "ordinary graphics property wrappers still unwrap")
(check (overlay-visible? (stree->tree '(show-from "2" "content")) 2)
       "presentation overlay visibility remains supported")
(check (not (overlay-visible? (stree->tree '(show-from "2" "content")) 1))
       "presentation overlays retain their step boundary")

(buffer-set-body (current-buffer)
  (stree->tree
    '(document
       (folded (document "Summary") (document "Details"))
       (screens (shown (document "First")) (hidden (document "Second")))
       (with "gr-geometry" (tuple "geometry" "4cm" "3cm" "center")
         (graphics (with "color" "red" (line (point "0" "0") (point "1" "1")))))
       (math (frac "a" "b")))))
(update-current-buffer)
(update-forced)
(tree-go-to (buffer-tree) 0 0 :start)
(alternate-toggle (tree-ref (buffer-tree) 0))
(check (tree-is? (buffer-tree) 0 'unfolded) "fold opens without a player")
(tree-go-to (buffer-tree) 1 0 0 :start)
(switch-to (tree-ref (buffer-tree) 1) :last)
(check (tree-is? (buffer-tree) 1 0 'hidden) "previous screen hides")
(check (tree-is? (buffer-tree) 1 1 'shown) "next screen opens without a player")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
