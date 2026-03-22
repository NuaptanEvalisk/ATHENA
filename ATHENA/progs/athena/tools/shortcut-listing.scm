
(texmacs-module (athena tools shortcut-listing)
  (:use (kernel athena tm-define)
        (kernel library list)
        (kernel gui kbd-define)
        (kernel boot ahash-table)))

(define (format-shortcut-entry entry)
  (let* ((key (car entry))
         (key-tree (tree->stree (kbd-system-rewrite key)))
         (ctx (cdr entry)))
    (map (lambda (c)
           (let* ((conds (car c))
                  (im (cdr c))
                  (cmd (car im))
                  (cmd-str (object->string cmd))
                  (conds-str (if (null? conds) "Global" (object->string conds))))
             `(row (cell ,key-tree) (cell ,cmd-str) (cell ,conds-str))))
         ctx)))

(tm-define (list-all-shortcuts)
  (:interactive #t)
  (let* ((l (ahash-table->list kbd-map-table))
         (l-sorted (sort l (lambda (x y) (string<=? (car x) (car y)))))
         (rows (append-map format-shortcut-entry l-sorted))
         (doc `(document
                 (section "ATHENA Keyboard Shortcuts")
                 (table
                   (tformat
                     (cwith 1 -1 1 -1 (cell-background "light grey"))
                     (cwith 1 1 1 -1 (cell-background "grey"))
                     (table
                       (row (cell (strong "Shortcut")) (cell (strong "Command")) (cell (strong "Context")))
                       ,@rows))))))
    (new-buffer)
    (buffer-set-body (current-buffer) (stree->tree doc))))
