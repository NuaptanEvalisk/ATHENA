;; Native math keybindings must preserve physical prefix expansion and shared
;; generic-key precedence inside displayed mathematics.
(init-style "generic")

(define (body) (tree->stree (buffer-tree)))
(define (check condition message)
  (unless condition (error message (body) (cursor-path))))
(define (ok? result) (eq? (car result) 'ok))
(define (reset content)
  (selection-cancel)
  (tree-set-diff (buffer-tree)
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

;; Equation/eqnarray structural conversion is an edit of the same source
;; object.  Its root identity and complete typed metadata must survive the
;; replacement, including undo/redo.
(reset '(concat "x" "=" "y"))
(define equation (tree-ref (buffer-tree) 0))
(define assigned (tree-ensure-node-id! equation))
(check (ok? assigned) "equation identity assignment failed")
(define equation-id (cadr assigned))
(define rich-property (stree->tree '(concat "metadata " (em "value"))))
(check
  (ok? (tree-update-node-properties!
         equation
         `(("test:marker" (string "preserve"))
           ("test:reference" (reference ,equation-id))
           ("test:rich" (rich-text ,rich-property)))
         '()))
  "equation property assignment failed")
(define equation-properties (tree-node-properties equation))

(keyboard-press "C-&" 0)
(commit-changes)
(define array (tree-ref (buffer-tree) 0))
(check (tree-is? array 'eqnarray*) "equation did not convert to eqnarray")
(check (equal? (tree-node-id array) equation-id)
       "equation-to-eqnarray lost root identity")
(check (equal? (tree-node-properties array) equation-properties)
       "equation-to-eqnarray lost root properties")

(undo 0)
(define undone-equation (tree-ref (buffer-tree) 0))
(check (tree-is? undone-equation 'equation*) "conversion undo did not restore equation")
(check (equal? (tree-node-id undone-equation) equation-id)
       "conversion undo lost root identity")
(check (equal? (tree-node-properties undone-equation) equation-properties)
       "conversion undo lost root properties")
(redo 0)
(define redone-array (tree-ref (buffer-tree) 0))
(check (tree-is? redone-array 'eqnarray*) "conversion redo did not restore eqnarray")
(check (equal? (tree-node-id redone-array) equation-id)
       "conversion redo lost root identity")
(check (equal? (tree-node-properties redone-array) equation-properties)
       "conversion redo lost root properties")

(tree-go-to (tree-ref redone-array 0 0 0 0 2 0) :end)
(keyboard-press "C-&" 0)
(define restored-equation (tree-ref (buffer-tree) 0))
(check (tree-is? restored-equation 'equation*) "eqnarray did not convert to equation")
(check (equal? (tree-node-id restored-equation) equation-id)
       "eqnarray-to-equation lost root identity")
(check (equal? (tree-node-properties restored-equation) equation-properties)
       "eqnarray-to-equation lost root properties")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
