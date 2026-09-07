(import-from (generic generic-edit) (generic format-edit) (math math-edit))
(module-provide '(athena keyboard prefix-kbd))
(module-provide '(generic generic-kbd))
(module-provide '(athena keyboard latex-kbd))
(lazy-keyboard-force #t)
(set-preference "look and feel" "kde")
(init-style "generic")

(define (check expected actual label)
  (unless (equal? expected actual)
    (error "Scheme command ordering regression" label expected actual)))

(define (reset content leaf-path)
  (selection-cancel)
  (buffer-set-body (current-buffer) (stree->tree `(document ,content)))
  (update-current-buffer)
  (update-forced)
  (apply tree-go-to
         (cons (buffer-tree) (append leaf-path (list :end)))))

(define (insert-bigcup-sub-alpha)
  (make-hybrid)
  (insert "bigcup")
  (hybrid-kbd-sub)
  (insert "<alpha>"))

(define (contains-stree? tree wanted)
  (or (equal? tree wanted)
      (and (pair? tree)
           (let loop ((children (cdr tree)))
             (and (pair? children)
                  (or (contains-stree? (car children) wanted)
                      (loop (cdr children))))))))

;; C-b uses the same immediate keyboard command conversion as ordinary
;; shortcuts.  It must finish changing the insertion environment before the
;; next key is processed.
(reset "x" '(0))
(key-press "C-b")
(check "bold" (get-env "font-series")
       "Ctrl+B was deferred behind the current actor command")

;; Reproduce the physical-key sequence at a fresh empty paragraph.  A block
;; formula entered while Ctrl+B is active must remain a block inside the
;; formatting wrapper; it must not acquire an extra DOCUMENT between WITH and
;; equation*.
(reset "" '(0))
(for-each key-press '("C-b" "\\" "[" "return"))
(check '(document
          (with "font-series" "bold"
            (equation* (document ""))))
       (tree->stree (buffer-tree))
       "bold display formula block nesting")

;; The same insertion at the end of an existing paragraph used to be dropped
;; entirely by make_return_after().  It must split the paragraph and keep the
;; block formula.
(reset "abc" '(0))
(for-each key-press '("C-b" "\\" "[" "return"))
(check '(document
          "abc"
          (with "font-series" "bold"
            (equation* (document ""))))
       (tree->stree (buffer-tree))
       "bold display formula after nonempty paragraph")

;; Continue the user-visible sequence through '(' and \bigcup.  The operator
;; must stay in the formula body and never become the child of rsub.
(reset "" '(0))
(for-each key-press
          '("C-b" "\\" "[" "return" "(" "\\"
            "b" "i" "g" "c" "u" "p" "return"))
(let ((tree (tree->stree (buffer-tree))))
  (unless (contains-stree? tree '(big "cup"))
    (error "Scheme command ordering regression"
           "bold display formula lost bigcup" tree))
  (when (contains-stree? tree '(rsub (big "cup")))
    (error "Scheme command ordering regression"
           "bold display formula placed bigcup inside rsub" tree)))

;; activate-latex is legacy synchronous editor code: after it returns, the
;; resolved Scheme command must already have mutated this BufferActor's tree.
(reset '(math "") '(0 0))
(make-hybrid)
(insert "bigcup")
(activate-latex)
(check '(document (math (big "cup")))
       (tree->stree (buffer-tree))
       "bigcup activation was deferred behind the current actor command")

;; In particular, the underscore typed immediately after a hybrid command must
;; attach to the already inserted operator.  Deferred self-dispatch used to
;; create the script first and later insert the big operator inside it.
(reset '(math "") '(0 0))
(insert-bigcup-sub-alpha)
(check '(document (math (concat (big "cup") (rsub "<alpha>"))))
       (tree->stree (buffer-tree))
       "inline bigcup subscript ordering")

;; The same ordering is required inside a bold environment.
(reset '(with "font-series" "bold" (math "")) '(0 2 0))
(insert-bigcup-sub-alpha)
(check '(document
          (with "font-series" "bold"
            (math (concat (big "cup") (rsub "<alpha>")))))
       (tree->stree (buffer-tree))
       "bold bigcup subscript ordering")

;; And inside equation-array cells, where the malformed tree is especially
;; visible because a big operator inside rsub is typeset on the script line.
(reset '(eqnarray*
          (document
            (tformat
              (table (row (cell "") (cell "=") (cell ""))))))
       '(0 0 0 0 0 0 0))
(insert-bigcup-sub-alpha)
(check '(document
          (eqnarray*
            (document
              (tformat
                (table
                  (row
                    (cell (concat (big "cup") (rsub "<alpha>")))
                    (cell "=")
                    (cell "")))))))
       (tree->stree (buffer-tree))
       "eqnarray bigcup subscript ordering")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))

