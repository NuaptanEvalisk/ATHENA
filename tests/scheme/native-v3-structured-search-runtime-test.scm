;; Exercise the actual C++ search dispatcher through its registered Scheme
;; interface, in an isolated headless ATHENA instance. No Vault is opened.
(define (check condition message)
  (unless condition (error "native-v3-search" message)))

(define (hits source query)
  (tree-search-tree source query '() 20))
(define (limited-hits source query limit)
  (tree-search-tree source query '() limit))

(define (make-math expression)
  (tree-rebuild (stree->tree '(math)) (list expression)))

(define (make-query expression)
  (make-math (tree-rebuild (stree->tree '(select-region))
                           (list expression))))

(define (with-property node key value)
  (check (eq? (car (tree-update-node-properties!
                    node (list (list key value)) '())) 'ok)
         "native property update rejected")
  node)

(check (null? (hits (stree->tree '(concat "a" "b"))
                    (stree->tree '(concat))))
       "empty CONCAT should not match source fragments")
(check (null? (hits (stree->tree "abc") (stree->tree '(concat))))
       "empty CONCAT should not match an atomic source")
(check (<= (length (hits (stree->tree "abc") (stree->tree ""))) 20)
       "empty atomic search exceeded its bounded result limit")
(check (= (length (limited-hits (stree->tree (make-string 48 #\a))
                                (stree->tree "a") 20)) 40)
       "hit limit counted range endpoints instead of complete matches")
(check (= (length (limited-hits (stree->tree (make-string 48 #\a))
                                (stree->tree "a") 1)) 2)
       "single-hit limit returned multiple matches")
(check (= (length (limited-hits (stree->tree '(document "aaaa" "aaaa" "aaaa"))
                                (stree->tree "a") 5)) 10)
       "hit limit was not shared correctly across document children")
(check (= (length (limited-hits (stree->tree '(concat "aaa" "aaa"))
                                (stree->tree "a") 4)) 8)
       "hit limit was not shared correctly across concat children")
(check (null? (limited-hits (stree->tree "abc") (stree->tree "a") 0))
       "zero hit limit unexpectedly returned a search range")
(check (not (null? (hits (stree->tree '(document "prefix abc suffix"))
                          (stree->tree "abc"))))
       "ordinary substring search regressed")

;; A document query containing one paragraph is a valid source subtree. In
;; particular, the multi-paragraph matching loop must not inspect query[1].
(check (not (null? (hits (stree->tree '(document "alpha" "beta"))
                          (stree->tree '(document "alpha")))))
       "single-paragraph DOCUMENT query lost its source match")
(check (null? (hits (stree->tree '(document "alpha" "beta"))
                    (stree->tree '(document "gamma"))))
       "single-paragraph DOCUMENT query matched unrelated content")
(check (not (null? (hits (stree->tree '(document "alpha" "beta" "gamma"))
                          (stree->tree '(document "alpha" "beta")))))
       "two-paragraph DOCUMENT search regressed")
(check (equal? (hits (stree->tree '(document "alpha" "beta" "gamma"))
                     (stree->tree '(document "alpha" "beta")))
               '((0 0) (1 4)))
       "two-paragraph DOCUMENT search returned the wrong source range")
(check (null? (hits (stree->tree '(document "alpha" "beta" "gamma"))
                    (stree->tree '(document "alpha" "delta"))))
       "two-paragraph DOCUMENT query matched a mismatched second paragraph")
(check (null? (hits (stree->tree '(document "alpha" "beta"))
                    (stree->tree '(document))))
       "empty DOCUMENT query unexpectedly selected source content")

;; A query attached to a specific source scope must still obey its typed
;; metadata constraint; the fast single-paragraph branch is only for ordinary
;; unannotated DOCUMENT queries.
(define scoped-document
  (with-property (stree->tree '(document "alpha" "beta"))
                 "query:scope" '(string "proof")))
(define scoped-query
  (with-property (stree->tree '(document "alpha"))
                 "query:scope" '(string "proof")))
(check (null? (hits (stree->tree '(document "alpha" "beta")) scoped-query))
       "attributed DOCUMENT query bypassed source metadata")
(check (not (null? (hits scoped-document scoped-query)))
       "matching attributed DOCUMENT query was lost")
(define scoped-concat
  (with-property (stree->tree '(concat "alpha" "beta"))
                 "query:scope" '(string "proof")))
(define scoped-concat-query
  (with-property (stree->tree '(concat "alpha"))
                 "query:scope" '(string "proof")))
(check (null? (hits (stree->tree '(concat "alpha" "beta"))
                    scoped-concat-query))
       "attributed CONCAT query bypassed source metadata")
(check (not (null? (hits scoped-concat scoped-concat-query)))
       "matching attributed CONCAT query was lost")

;; The SELECT_REGION matcher also invokes match() directly. A nested pattern
;; with properties must not match a bare source atom merely because their
;; flattened strings agree; the typed constraint must survive this path.
(define attributed-concat-prefix
  (with-property (stree->tree '(concat "nee" "dle"))
                 "query:scope" '(string "proof")))
(define attributed-blank-prefix
  (with-property (stree->tree "")
                 "query:scope" '(string "proof")))
(define (document-selection-prefix prefix)
  (tree-rebuild (stree->tree '(document))
                (list prefix (stree->tree '(select-region "anchor")))))
(check (null? (hits (stree->tree '(document "needle" "anchor"))
                    (document-selection-prefix attributed-concat-prefix)))
       "typed nested CONCAT constraint bypassed via match_cascaded")
(check (null? (hits (stree->tree '(document "needle" "anchor"))
                    (document-selection-prefix attributed-blank-prefix)))
       "typed empty prefix acted as unconstrained blank match")
(define matching-typed-prefix
  (with-property (stree->tree '(concat "nee" "dle"))
                 "query:scope" '(string "proof")))
(define typed-prefix-source
  (tree-rebuild (stree->tree '(document))
                (list matching-typed-prefix (stree->tree "anchor"))))
(check (not (null? (hits typed-prefix-source
                          (document-selection-prefix attributed-concat-prefix))))
       "valid typed CONCAT prefix was rejected by select-region matching")
(check (not (null? (hits (stree->tree '(document "needle" "anchor"))
                          (document-selection-prefix
                           (stree->tree '(concat "nee" "dle"))))))
       "unattributed CONCAT prefix lost legacy text pattern matching")

(define script (with-property (stree->tree '(scripts "x" "" "" "i" "2"))
                 "math:scripts-policy" '(string "r")))
(define same (with-property (stree->tree '(scripts "x" "" "" "i" "2"))
               "math:scripts-policy" '(string "r")))
(define other (with-property (stree->tree '(scripts "x" "" "" "i" "2"))
                "math:scripts-policy" '(string "o")))

(check (not (null? (hits (make-math script) (make-query same))))
       "matching script policy had no hit")
(check (null? (hits (make-math script) (make-query other)))
       "different script placement unexpectedly matched")

(define (grid kind cells)
  (with-property (tree-rebuild (stree->tree '(math-grid))
                 (list (tree-rebuild (stree->tree '(row)) cells)))
                 "math:grid-kind" (list 'string kind)))
(define cell-x (stree->tree '(cell "x")))
(define cell-y (stree->tree '(cell "y")))
(define cell-empty (stree->tree '(cell "")))
(define source (grid "align" (list cell-x cell-y)))
(define matching (grid "align" (list cell-x cell-y)))
(check (not (null? (hits (make-math source) (make-query matching))))
       "identical two-column grid had no hit")
(check (null? (hits (make-math source)
                    (make-query (grid "align" (list cell-x)))))
       "grid with an extra cell unexpectedly matched")
(check (null? (hits (make-math source)
                    (make-query (grid "align" (list cell-x cell-empty)))))
       "empty grid cell acted as a wildcard")
(check (null? (hits (make-math source)
                    (make-query (grid "eqnarray" (list cell-x cell-y)))))
       "different grid kind unexpectedly matched")

;; Native ROW numbering and rich tags are typed source properties, not text
;; children. Missing numbering means inherit. Matching rich tag content must
;; not require the same persistent UUID as the original source node.
(define (grid-with-row-properties numbering tag)
  (let ((row (stree->tree '(row (cell "x") (cell "y")))))
    (when numbering (with-property row "math:row-numbering"
                                   (list 'string numbering)))
    (when tag (with-property row "math:row-tag"
                             (list 'rich-text tag)))
    (with-property (tree-rebuild (stree->tree '(math-grid)) (list row))
                   "math:grid-kind" '(string "align"))))
(define tag-source (stree->tree '(strong "Lemma")))
(define tag-query (stree->tree '(strong "Lemma")))
(check (eq? (car (tree-ensure-node-id! tag-source)) 'ok)
       "could not assign source rich tag identity")
(check (eq? (car (tree-ensure-node-id! tag-query)) 'ok)
       "could not assign query rich tag identity")
(define tagged-grid (grid-with-row-properties "unnumbered" tag-source))
(define tagged-same (grid-with-row-properties "unnumbered" tag-query))
(define tagged-numbered (grid-with-row-properties "numbered" tag-query))
(define tagged-inherit (grid-with-row-properties #f tag-query))
(define tagged-other (grid-with-row-properties
                      "unnumbered" (stree->tree '(strong "Theorem"))))
(check (not (null? (hits (make-math tagged-grid) (make-query tagged-same))))
       "equal rich row tags with different UUIDs were treated as unequal")
(check (null? (hits (make-math tagged-grid) (make-query tagged-numbered)))
       "math row numbering policy was ignored")
(check (null? (hits (make-math tagged-grid) (make-query tagged-inherit)))
       "inherited math row policy matched explicit unnumbered")
(check (null? (hits (make-math tagged-grid) (make-query tagged-other)))
       "different rich row tags matched")

;; CELL format is a typed dictionary, not a presentation string. Different
;; dictionaries or a missing CELL-format property cannot match the same query.
(define (grid-with-cell-color color)
  (let ((cell (stree->tree '(cell "x"))))
    (when color
      (with-property cell "math:cell-format"
        (list 'dictionary (list (list "color" (list 'string color))))))
    (with-property
      (tree-rebuild (stree->tree '(math-grid))
        (list (tree-rebuild (stree->tree '(row)) (list cell))))
      "math:grid-kind" '(string "matrix"))))
(define colored-cell (grid-with-cell-color "blue"))
(check (not (null? (hits (make-math colored-cell)
                          (make-query (grid-with-cell-color "blue")))))
       "matching typed cell-format dictionary was rejected")
(check (null? (hits (make-math colored-cell)
                    (make-query (grid-with-cell-color "red"))))
       "different typed cell-format values matched")
(check (null? (hits (make-math colored-cell)
                    (make-query (grid-with-cell-color #f))))
       "missing typed cell-format matched an explicit source format")

;; SELECT_REGION is a query instruction, not a replacement for structural
;; matching of the enclosing native math node. Invisible bracket endpoints
;; and other empty descriptors must not act as blank-match wildcards.
(define (delimited left right body)
  (stree->tree `(delimited ,left ,right "a" "" "" ,body)))
(define selected-x (stree->tree '(select-region "x")))
(check (not (null? (hits (make-math (delimited "(" ")" "x"))
                          (make-math (delimited "(" ")" selected-x)))))
       "selected text inside equal delimiters disappeared")
(check (equal? (hits (stree->tree '(math (delimited "(" ")" "a" "" "" "xyz")))
                     (stree->tree '(math (delimited "(" ")" "a" "" ""
                                                  (select-region "xy")))))
               '((0 5 0) (0 5 2)))
       "math selection returned descriptor or a wrong UTF-8 source path")
(check (equal? (hits (stree->tree '(math (delimited "(" ")" "a" "" "" "αβγ")))
                     (stree->tree '(math (delimited "(" ")" "a" "" ""
                                                  (select-region "β")))))
               '((0 5 2) (0 5 4)))
       "math selection positions used characters instead of UTF-8 bytes")
(check (null? (hits (make-math (delimited "(" ")" "x"))
                    (make-math (delimited "" ")" selected-x))))
       "invisible left delimiter matched a visible endpoint")
(check (null? (hits (make-math (delimited "(" ")" "x"))
                    (make-math (delimited "(" "" selected-x))))
       "invisible right delimiter matched a visible endpoint")

;; A subexpression selection inside a grid must not disregard other cells or
;; silently relocate the selected cell to a different column.
(define selected-cell (stree->tree '(cell (select-region "x"))))
(check (not (null? (hits (make-math source)
                          (make-math (grid "align" (list selected-cell cell-y))))))
       "selection within a matching two-cell grid disappeared")
(check (null? (hits (make-math source)
                    (make-math (grid "align" (list selected-cell)))))
       "selection inside a shorter grid ignored the extra column")

;; Vault content search wraps a mathematical expression in SELECT_REGION to
;; return only the expression. An already structured selection must survive
;; this second query-level wrapper instead of becoming an inert source tag.
(check (not (null? (hits (make-math source)
                          (make-query (grid "align" (list selected-cell cell-y))))))
       "nested selection was lost behind the Vault math wrapper")
(check (null? (hits (make-math source)
                    (make-query (grid "align" (list selected-cell)))))
       "nested selection bypassed the MathGrid column count")

;; Matching regular inline content remains permissive around the selected
;; fragment; these are not native mathematics descriptor slots.
(check (not (null? (hits (stree->tree '(concat "prefix" "x" "suffix"))
                          (stree->tree '(concat "prefix" (select-region "x") "suffix")))))
       "ordinary inline structured selection regressed")

;; Script placement and variant kind are strict even if only one nested
;; expression is selected for display in the results.
(define selected-script
  (with-property
    (stree->tree '(scripts (select-region "x") "" "" "i" "2"))
    "math:scripts-policy" '(string "r")))
(check (not (null? (hits (make-math script) (make-query selected-script))))
       "matching script selection disappeared")
(check (null? (hits (make-math other) (make-query selected-script)))
       "script selection disregarded placement policy")
(check (not (null? (hits (make-math (stree->tree '(math-variant "rm" "x")))
                          (make-query (stree->tree '(math-variant "rm" (select-region "x")))))))
       "matching MathVariant selection disappeared")
(check (null? (hits (make-math (stree->tree '(math-variant "bf" "x")))
                    (make-query (stree->tree '(math-variant "rm" (select-region "x"))))))
       "MathVariant selection disregarded its kind descriptor")

;; The source editor exposes all physical children, including descriptors,
;; but text search must still see only editable math expression content.
;; A complete structured math query must nevertheless retain descriptor checks.
(define (with-source-access thunk)
  (let ((previous (set-access-mode 2)))
    (dynamic-wind (lambda () #t) thunk
                  (lambda () (set-access-mode previous)))))
(with-source-access
 (lambda ()
   (let ((brackets (make-math (delimited "(" ")" "x")))
         (variant (make-math (stree->tree '(math-variant "rm" "x")))))
     (check (null? (hits brackets (stree->tree "a")))
            "math delimiter sizing policy leaked into text search")
     (check (null? (hits brackets (stree->tree "(")))
            "math delimiter left descriptor leaked into text search")
     (check (null? (hits variant (stree->tree "rm")))
            "MathVariant kind descriptor leaked into text search")
     (check (not (null? (hits brackets (stree->tree "x"))))
            "math expression body was incorrectly hidden from text search")
     (check (not (null? (hits brackets
                            (make-query (delimited "(" ")" "x")))))
            "complete structured delimiter query was blocked")
     (let ((manual (make-math
                   (stree->tree '(delimited "(" ")" "m" "1024" "1024" "x")))))
       (check (null? (hits manual (stree->tree "1024")))
              "manual delimiter size leaked into text search")
       (check (not (null? (hits manual (stree->tree "x"))))
              "manual delimiter body was hidden from text search"))
     (let ((sum (make-math (stree->tree '(big-operator "∑" "0"))))
           (middle (make-math (stree->tree '(math-middle "|"))))
           (arrow (make-math (stree->tree '(math-long-arrow "→" "" "")))))
       (check (not (null? (hits sum (stree->tree "∑"))))
              "mathematical operator glyph disappeared from text search")
       (check (null? (hits sum (stree->tree "0")))
              "BigOperator upright descriptor leaked into text search")
       (check (not (null? (hits middle (stree->tree "|"))))
              "middle separator glyph disappeared from text search")
       (check (not (null? (hits arrow (stree->tree "→"))))
              "long-arrow glyph disappeared from text search")))))
