;; Load in an initialized ATHENA runtime. This exercises the same
;; generic->texmacs / <format>-snippet dispatch used by selection_paste.
;; No editor, source files, system clipboard or vault are modified.
(define (check value message)
  (unless value (error "Native v3 MathML routing regression" message)))
(define (expect-error thunk reason)
  (let ((caught (catch 'misc-error (lambda () (thunk) #f)
                  (lambda args args))))
    (check (and caught (string-contains? (object->string caught) reason))
           "unsupported MathML did not propagate its native diagnostic")))
(define (mathml-node-property tree key)
  (cadr (assoc key (tree-node-properties tree))))
(define (contains-tag? tree tag)
  (and (pair? tree)
       (or (eq? (car tree) tag)
           (list-any (lambda (child) (contains-tag? child tag)) (cdr tree)))))

;; Do not import init-mathml here: the public conversion registry must load it.
(for-each
  (lambda (source)
    (check (equal? (converter-search source "texmacs-tree")
                   (list source "texmacs-tree"))
           "native MathML route is missing or uses an intermediate format"))
  '("mathml-v3-snippet" "mathml-v3-document" "mathml-v3-stree"))
(check (in? "mathml-v3"
            (converters-to-special "texmacs-tree" "-snippet" #t))
       "native MathML format is absent from paste-as converters")

(define xml
  (string-append
    "<math><munderover><mo>&sum;</mo><mi>i</mi><mi>n</mi>"
    "</munderover></math>"))
(for-each
  (lambda (format)
    (let* ((result (generic->texmacs xml format))
           (scripts (tree-ref result 0)))
      (check (tree? result) "converter returned a stree, not a native tree")
      (check (eq? (tree-label scripts) 'scripts) "legacy script layout reached paste")
      (check (equal? (mathml-node-property scripts "math:scripts-policy") '(string "d"))
             "typed script placement was lost")
      (check (eq? (tree-label (tree-ref scripts 0)) 'big-operator)
             "legacy big operator reached paste")
      (check (not (contains-tag? (tree->stree result) 'rsub))
             "legacy script node reached native conversion")))
  '("mathml-v3-snippet" "mathml-v3-document"))
(let* ((result (convert (parse-xml xml) "mathml-v3-stree" "texmacs-tree"))
       (scripts (tree-ref result 0)))
  (check (equal? (mathml-node-property scripts "math:scripts-policy") '(string "d"))
         "parsed MathML route lost metadata"))

(define formatted
  (generic->texmacs
    (string-append
      "<math display='block'><mtable columnalign='left right'><mtr>"
      "<mtd mathcolor='red'><mi>x</mi></mtd><mtd><mi>y</mi></mtd>"
      "</mtr></mtable></math>")
    "mathml-v3-snippet"))
(check (eq? (tree-label formatted) 'math-block) "display became a legacy equation")
(check (equal? (mathml-node-property formatted "math:block-numbered") '(boolean #f))
       "display numbering metadata was lost")
(let* ((grid (tree-ref formatted 0)) (left (tree-ref grid 0 0))
       (fields (cadr (mathml-node-property left "math:cell-format")))
       (right-fields
         (cadr (mathml-node-property (tree-ref grid 0 1) "math:cell-format"))))
  (check (equal? (mathml-node-property grid "math:grid-kind") '(string "generic"))
         "generic table kind was guessed or lost")
  (check (equal? (cadr (assoc "align" fields)) '(string "l"))
         "canonical left-cell alignment was lost")
  (check (equal? (cadr (assoc "align" right-fields)) '(string "r"))
         "canonical right-cell alignment was lost")
  (check (equal? (cadr (assoc "color" fields)) '(string "red"))
         "cell color was lost"))

(define sup "<math><msup><mi>x</mi><mn>2</mn></msup></math>")
(check (contains-tag? (tree->stree (native-mathml->tree (parse-xml sup))) 'rsup)
       "legacy native MathML entry changed")
(check (contains-tag? (convert sup "html-snippet" "texmacs-stree") 'rsup)
       "automatic HTML MathML import changed before cutover")
(for-each
  (lambda (format)
    (expect-error
      (lambda () (generic->texmacs
                   "<math><semantics><mi>x</mi><annotation encoding='application/x-tex'>x^2</annotation></semantics></math>"
                   format))
      "annotation")
    (expect-error
      (lambda () (generic->texmacs "<math><maction><mi>x</mi></maction></math>" format))
      "maction"))
  '("mathml-v3-snippet" "mathml-v3-document"))
(display "PASS: explicit native MathML document/paste routes, typed metadata and v2 isolation\n")
