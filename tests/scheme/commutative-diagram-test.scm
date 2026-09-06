(import-from (athena athena commutative-diagram))
(for-each
  (lambda (name)
    (module-define! (current-module) name
      (module-ref (resolve-module '(athena athena commutative-diagram)) name)))
  '(cd-find-arrow cd-arrow-geometry cd-begin-session cd-select
    cd-set-selected-option cd-point+ cd-point* cd-bezier-point
    cd-nearest-arrow cd-arrow-id cd-reverse-selected-arrow cd-option
    cd-clear-selection))
(init-style "generic")

(define (check condition name)
  (unless condition (error "Diagram regression" name)))
(define (near a b) (< (abs (- a b)) 0.000001))
(define (same-point? p q)
  (and (near (car p) (car q)) (near (cadr p) (cadr q))))
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

;; Real mutable editor tree and the same property command used by the dialog.
(buffer-set-body (current-buffer)
  (stree->tree `(document ,(sample "left" "0" "2" "1"))))
(update-current-buffer)
(update-forced)
(let* ((body (tree-ref (buffer-tree) 0 0 2))
       (arrow (cd-find-arrow body "f"))
       (baseline (cd-arrow-geometry body arrow))
       (length (sqrt 34)) (normal (list (/ -3 length) (/ 5 length))))
  (cd-begin-session body)
  (cd-select 'arrow "f")
  (for-each
    (lambda (offset)
      (cd-set-selected-option "offset" (number->string offset))
      (let ((shifted (cd-arrow-geometry body arrow)))
        (for-each
          (lambda (p q)
            (check (same-point? q (cd-point+ p (cd-point* normal (* .08 offset))))
                   "all control points move transversely"))
          baseline shifted)
        (let* ((middle (cd-bezier-point shifted .5))
               (hit (cd-nearest-arrow body (car middle) (cadr middle) .01)))
          (check (and hit (equal? (cd-arrow-id hit) "f")) "shifted edge hit test"))))
    '(-5 -2 0 3 5))
  (let* ((before (cd-arrow-geometry body arrow))
         (serial (tree->stree body)))
    (check (equal? serial (tree->stree (stree->tree serial)))
           "options survive tree serialization")
    (cd-reverse-selected-arrow)
    (for-each (lambda (p q)
                (check (same-point? p q)
                       "reversing preserves the displaced curved route"))
      before (reverse (cd-arrow-geometry body arrow)))
    (check (equal? (cd-option arrow "label-alignment" "") "right")
           "reversing preserves label side")
    (check (same-point? (cd-bezier-point before .25)
                       (cd-bezier-point (cd-arrow-geometry body arrow)
                         (* .01 (string->number
                                  (cd-option arrow "label-position" "50")))))
           "reversing preserves the label position")))
(cd-clear-selection)

;; Real native boxes and PDF output for visual verification of all alignments.
(buffer-set-body (current-buffer)
  (stree->tree
    `(document
       "Left label, diagonal arrow" ,(sample "left" "0" "0" "1")
       "Positive transverse offset" ,(sample "left" "3" "0" "1")
       "Negative transverse offset" ,(sample "right" "-3" "0" "1")
       "Centre: horizontal with a gap" ,(sample "centre" "0" "0" "1")
       "Over: rotated with the arrow" ,(sample "over" "0" "0" "1")
       "Curved double edge" ,(sample "left" "0" "3" "2")
       "Reversed curve, four shafts" ,(sample "right" "0" "-3" "4"))))
(init-env "font" "TeX Gyre Pagella")
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
