(import-from (math math-edit) (utils edit selections) (table table-edit))
(init-style "generic")

(define fixture
  '(eqnarray* (document (tformat
     (table (row (cell "a") (cell "=") (cell "b"))
            (row (cell "c") (cell "=") (cell "d")))))))
(define (check expected actual label)
  (unless (equal? expected actual)
    (error "Eqnarray clipboard regression" label expected actual)))
(define (reset content)
  (selection-cancel)
  (buffer-set-body (current-buffer) (stree->tree `(document ,content)))
  (update-current-buffer)
  (update-forced)
  (tree-go-to (buffer-tree) 0 :start))

(define (select-tree t)
  (selection-set (tree->path t :start) (tree->path t :end)))
(define first-row '(row (cell "a") (cell "=") (cell "b")))
(define row-fragment `(eqnarray* (document (tformat (table ,first-row)))))
(define (paste-check expected label)
  (reset "")
  (clipboard-paste "eqnarray-test")
  (check `(document ,expected) (tree->stree (buffer-tree)) label))

(reset fixture)
(let ((format (tree-ref (buffer-tree) 0 0 0)))
  (selection-set (tree->path format :start) (tree->path format :end)))
(check fixture (tree->stree (selection-tree)) "whole layout table selects eqnarray")
(clipboard-copy "eqnarray-test")
(reset "")
(clipboard-paste "eqnarray-test")
(check `(document ,fixture) (tree->stree (buffer-tree)) "whole paste")

(reset fixture)
(select-tree (tree-ref (buffer-tree) 0 0 0))
(clipboard-copy "primary")
(reset "")
(clipboard-paste "primary")
(check `(document ,fixture) (tree->stree (buffer-tree))
       "primary clipboard export and internal headless paste")

(for-each
  (lambda (tag)
    (let ((source (cons tag (cdr fixture))))
      (for-each
        (lambda (indices)
          (reset source)
          (let ((t (apply tree-ref (buffer-tree) indices)))
            (select-tree t))
          (check source (tree->stree (selection-tree))
                 "whole document, format or table selects its owner")
          (clipboard-cut "eqnarray-test")
          (check '(document "") (tree->stree (buffer-tree)) "whole cut leaves no shell")
          (paste-check source "whole cut and paste"))
        '((0 0) (0 0 0) (0 0 0 0)))))
  '(eqnarray eqnarray*))

(reset fixture)
(let* ((table (tree-ref (buffer-tree) 0 0 0 0))
       (first (tree-ref table 0 0 0)) (last (tree-ref table 1 2 0)))
  (tree-go-to last :end)
  (select-from-keyboard #t)
  (tree-go-to first :start)
  (select-from-cursor)
  (select-from-keyboard #f))
(check fixture (tree->stree (selection-tree)) "reverse keyboard selection across all cells")

(reset fixture)
(select-tree (tree-ref (buffer-tree) 0 0 0 0 0))
(clipboard-copy "eqnarray-test")
(paste-check row-fragment "partial row pastes as eqnarray")

;; The same fragment fills cells when pasted into another eqnarray, not a
;; nested display environment within a cell.
(reset fixture)
(tree-go-to (buffer-tree) 0 0 0 0 1 0 0 :start)
(clipboard-paste "eqnarray-test")
(check `(document (eqnarray* (document (tformat (table ,first-row ,first-row)))))
       (tree->stree (buffer-tree)) "paste row into existing equation array")

(reset fixture)
(select-tree (tree-ref (buffer-tree) 0 0 0 0 0))
(clipboard-cut "eqnarray-test")
(check 'eqnarray* (tree-label (tree-ref (buffer-tree) 0)) "partial cut retains owner")
(paste-check row-fragment "partial cut keeps equation semantics")

(reset fixture)
(tree-go-to (buffer-tree) 0 0 0 0 0 1 0 :start)
(table-select-cells 1 1 2 2)
(clipboard-copy "eqnarray-test")
(paste-check '(eqnarray* (document (tformat (table (row (cell "="))))))
             "complete single cell retains equation semantics")

;; Ordinary tables and text selections within one formula are unchanged.
(reset `(tabular (tformat (table ,first-row (row (cell "c") (cell "=") (cell "d"))))))
(select-tree (tree-ref (buffer-tree) 0 0 0 0))
(clipboard-copy "eqnarray-test")
(paste-check `(tabular (tformat (table ,first-row))) "ordinary table row")

(reset fixture)
(select-tree (tree-ref (buffer-tree) 0 0 0 0 0 0 0))
(clipboard-copy "eqnarray-test")
(paste-check '(math "a") "ordinary inline mathematical text")

(define inner-format '(tformat (table (row (cell "x") (cell "y")))))
(reset
  `(eqnarray* (document (tformat (table
    (row (cell (matrix ,inner-format)) (cell "=") (cell "z")))))))
(select-tree (tree-ref (buffer-tree) 0 0 0 0 0 0 0 0))
(check inner-format
       (tree->stree (selection-tree)) "nested matrix does not select outer eqnarray")
(clipboard-copy "eqnarray-test")
(paste-check `(math (tabular ,inner-format))
             "nested matrix clipboard remains independent")

(reset fixture)
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
