;; Exercise real editor mutations in the owning BufferActor, not mocked glue.
(import-from (math math-edit))
(init-style "generic")

(define (check expected actual label)
  (unless (equal? expected actual)
    (error "Evaluation bar regression" label expected actual)))

(define (body)
  (buffer-tree))

(define (trace label)
  (with port (open-file (string-append (getenv "HOME") "/trace.scm") "a")
    (write (list label (tree->stree (body)) (cursor-path)) port)
    (newline port)
    (close-port port)))

(define (reset-math content)
  (selection-cancel)
  (buffer-set-body (current-buffer) (stree->tree `(document (math ,content))))
  (update-current-buffer)
  (update-forced)
  (tree-go-to (body) 0 0 :end)
  (check "math" (get-env "mode") "math environment initialized")
  (trace 'reset))

(define (check-math expected label)
  (trace label)
  (check `(document (math ,expected)) (tree->stree (body)) label))

(reset-math '(frac "d" "d x"))
(math-evaluation-bar)
(check-math '(around* "<nobracket>" (frac "d" "d x") "|") "fraction")
(make-script #f #t)
(insert "t=0")
(check-math '(concat (around* "<nobracket>" (frac "d" "d x") "|")
                     (rsub "t=0")) "limit is outside the pair")

(reset-math '(concat "x" (rsup "2")))
(math-evaluation-bar)
(check-math '(around* "<nobracket>" (concat "x" (rsup "2")) "|")
            "script remains attached to its base")

(reset-math '(concat (frac "d" "d x") "f" (around* "(" "x" ")")))
(math-evaluation-bar)
(check-math '(around* "<nobracket>"
                     (concat (frac "d" "d x") "f" (around* "(" "x" ")")) "|")
            "whole preceding expression, not only its final operand")

(reset-math "x+y+z")
(tree-go-to (body) 0 0 3)
(math-evaluation-bar)
(check-math '(concat (around* "<nobracket>" "x+y" "|") "+z")
            "following content is preserved")

(reset-math '(frac "x" "y+z"))
(tree-go-to (body) 0 0 1 :end)
(math-evaluation-bar)
(check-math '(frac "x" (around* "<nobracket>" "y+z" "|"))
            "fraction argument boundary")

(reset-math '(concat "a+" (frac "b" "c")))
(with math-body (tree-ref (body) 0 0)
  (selection-set (tree->path math-body :start)
                 (tree->path math-body :end)))
(math-evaluation-bar)
(check-math '(around* "<nobracket>" (concat "a+" (frac "b" "c")) "|")
            "explicit selection")

(reset-math "")
(math-evaluation-bar)
(insert '(frac "x" "y"))
(check-math '(around* "<nobracket>" (frac "x" "y") "|")
            "empty insertion starts inside")

(tree-go-to (body) 0 0 :end)
(math-evaluation-bar)
(check-math '(around* "<nobracket>"
                     (around* "<nobracket>" (frac "x" "y") "|") "|")
            "nested evaluation")

;; Export the same editable representation, including a limit and nesting.
(buffer-set-body
  (current-buffer)
  (stree->tree
    '(document
      (math (concat (around* "<nobracket>" "x" "|") (rsub "t=0")))
      (math (concat (around* "<nobracket>" (frac "d" "d x") "|")
                    (rsub "t=0")))
      (math (concat (around* "<nobracket>"
                            (frac (frac "x" "y") "z") "|")
                    (rsub "t=0")))
      (math (around* "<nobracket>"
                     (around* "<nobracket>" (frac "x" "y") "|") "|")))))
(init-env "font" "TeX Gyre Pagella")
(init-env "math-display" "true")
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
