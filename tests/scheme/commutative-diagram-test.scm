(init-style "generic")

(define (sample alignment offset curve level)
  `(math
     (commutative-diagram "8" "5"
       (cd-body ""
         (cd-vertex "a" "-2.5" "-1.5" "A")
         (cd-vertex "b" "2.5" "1.5" "B")
         (cd-arrow "f" "a" "b"
           (frac (concat "F" (around* "(" "X" ")"))
                 (concat "G" (around* "(" "Y" ")")))
           (tuple "label-alignment" ,alignment "offset" ,offset
                  "curve" ,curve "level" ,level
                  "label-position" "25" "label-color" "red"))))))

(define long-vertex-sample
  `(math
     (commutative-diagram "4" "2"
       (cd-body ""
         (cd-vertex "long-left" "-1.7" "0"
           (concat "(" "F" "<circ>" "G" ")" "A"))
         (cd-vertex "long-right" "1.7" "0"
           (concat "id" "A" "(" "A" ")"))
         (cd-arrow "long-arrow" "long-left" "long-right" "id"
           (tuple "label-alignment" "left" "label-position" "50"))))))

;; Persistent AST only: rendering is entirely native C++ and no diagram Scheme
;; implementation is imported or invoked here.
(buffer-set-body (current-buffer)
  (stree->tree
    `(document
       "Left label, diagonal arrow" ,(sample "left" "0" "0" "1")
       "Positive transverse offset" ,(sample "left" "3" "0" "1")
       "Negative transverse offset" ,(sample "right" "-3" "0" "1")
       "Centre: horizontal with a gap" ,(sample "centre" "0" "0" "1")
       "Over: rotated with the arrow" ,(sample "over" "0" "0" "1")
       "Curved double edge" ,(sample "left" "0" "3" "2")
       "Reversed curve, four shafts" ,(sample "right" "0" "-3" "4")
       "Long vertex labels expand bounds and shorten arrows" ,long-vertex-sample)))
(init-env "font" "TeX Gyre Pagella")
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
