;; Render the actual native XML v3 mathematical alphabets through the owning
;; editor, layout engine and PDF renderer in an isolated anonymous buffer.
;; A source-only codec/LaTeX check cannot show that the math fonts were
;; selected or that the new MATH_VARIANT children remain printable.
(init-style "generic")
(define sample
  '(document
     (math (concat (math-variant "mbf" "A") " + "
                   (math-variant "mbfu" "B") " + "
                   (math-variant "frak" "C")))
     (math (concat (math-variant "bb" "R") " + "
                   (math-variant "cal" "F") " + "
                   (math-variant "bf" "T")))))
(buffer-set-body (current-buffer) (stree->tree sample))
(unless (equal? (tree->stree (buffer-tree)) sample)
  (error "native math variant body changed before typesetting"))
(init-env "font" "TeX Gyre Pagella")
(init-env "math-display" "true")
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
