;; Website export must not depend on a UI-selected buffer.
(set-preference "document save mode" "manual")
(define (check condition message)
  (unless condition (error message)))
(use-modules (convert html htmltm))
(define (contains-node? expected value)
  (or (equal? expected value)
      (and (pair? value)
           (or (contains-node? expected (car value))
               (contains-node? expected (cdr value))))))
(define imported-table
  (html->texmacs
    (parse-html-snippet
      "<table width='50%' border='2' bgcolor='aqua'><tr><td rowspan='2'><p>A</p><p>B</p></td><td>C</td></tr><tr><td><font color='green'>D</font></td></tr></table>")))
(define spanning-table
  (html->texmacs
    (parse-html-snippet
      "<table><tr><td rowspan='2' colspan='2'>A</td><td>B</td></tr><tr><td>C</td></tr></table>")))
(check
  (contains-node? '(row (cell (document "")) (cell (document ""))
                       (cell (document "C"))) spanning-table)
  "table occupancy placed a cell inside a combined row/column span")
(for-each
  (lambda (expected)
    (check (contains-node? expected imported-table)
           (string-append "HTML table import lost " (object->string expected))))
  '((twith "table-width" "0.5par")
    (twith "table-lborder" "2px")
    (cwith "1" "-1" "1" "-1" "cell-background" "#00FFFF")
    (cwith "1" "1" "1" "1" "cell-row-span" "2")
    (with "color" "#008000" "D")
    (cell (document "A" "B"))))
(check
  (equal? (native-html-import-serial
            '("  " (label "before") " first " (next-line) " second "
              (document (quotation (document "block"))) " tail ") #f)
          '(document (concat (label "before") "first" (next-line) "second")
                     (quotation (document "block")) "tail"))
  "import paragraph assembly misplaced anchors or line boundaries")
(check
  (equal? (native-html-import-element "list-body" '()
            '(document (item) "first" "continuation" (item*) (item) "last"))
          '(document (concat (item) "first") "continuation" (item*)
                     (concat (item) "last")))
  "import list assembly swallowed a continuation or consecutive marker")
(check
  (equal? (native-html-import-element "label" '((target "heading"))
            '(document (section "Title") "body"))
          '(document (concat (section "Title") (label "heading")) "body"))
  "import heading destination was placed inside the title")
;; The parser owns PRE's first-newline rule; semantic import must not repeat it.
(check
  (equal? (native-html-import-whitespace "pre" '("\nkept\n\n") #f)
          '((h:p "") (h:p "kept") (h:p "") (h:p "")))
  "semantic import discarded PRE blank lines")
(check
  (equal? (native-html-import-whitespace "mixed"
            '("  a" "\t b " (h:strong "c") " d\n ") #f)
          '("a b " (h:strong "c") " d"))
  "mixed-content import lost text-run boundary spaces")
(check
  (equal? (native-html-import-element "img"
            '((src "figure.svg") (width "50%") (height "24")) "")
          '(image "figure.svg" "0.5par" "24px" "" ""))
  "native import image dimensions are not available through the binding")
(define source (string->url (string-append (getenv "HOME") "/source.ath")))
(define target (string->url (string-append (getenv "HOME") "/source.html")))
(define payload (string-append "Unicode " (string #\x03c0 #\x2208 #\x2124)
                               " & < followed by several words"))
(define image-bytes
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"30\"><rect width=\"20\" height=\"30\" fill=\"red\"/></svg>")
(string-save image-bytes (url-relative source "asset.svg"))
(define semantic-fixture
  '(n (@ (t "enunciation"))
     (x (p (@ (n "kind") (t "s")) "definition")
        (p (@ (n "numbered") (t "b")) "false")
        (p (@ (n "name") (t "h"))
           (n (@ (t "strong")) (t (v "Native semantic title"))))
        (p (@ (n "year") (t "s")) "1827"))
     (n (@ (t "document"))
        (t (v "Semantic body remains visible."))
        (n (@ (t "math-block") (b "0"))
           (n (@ (t "math-grid") (k "stack"))
              (n (@ (t "row"))
                 (n (@ (t "cell"))
                    (n (@ (t "scripts") (p "r"))
                       (t (v "x")) (t (v "")) (t (v ""))
                       (t (v "i")) (t (v "2"))))))))))
(set-preference "texmacs->html:mathjax" "on")
(string-save
  (serialize-xml
    `(*TOP* (a (@ (v "3") (k "d"))
       (n (@ (t "document"))
         (n (@ (t "style")) (n (@ (t "tuple")) (t (v "generic"))))
         (n (@ (t "body"))
            (n (@ (t "document")) (t (v ,payload)) ,semantic-fixture
               (n (@ (t "image")) (t (v "asset.svg"))
                  (t (v "20px")) (t (v "30px")) (t (v "")) (t (v "")))))))))
  source)
(check (not (current-buffer)) "fixture unexpectedly selected a buffer")
(check (not (buffer-load source))
       (string-append "load explicit export buffer: " (string-load source)))
(check (not (buffer-export source target "html")) "export without UI focus")
(check (string-contains? (string-load target) "followed by several words")
       "HTML export lost text after Unicode")
(check (string-contains? (string-load target) "&amp; &lt; followed")
       "semantic HTML export escaped text more than once")
(for-each
  (lambda (expected)
    (check (string-contains? (string-load target) expected)
           (string-append "native semantic HTML export lost: " expected)))
  '("Native semantic title" "1827" "Semantic body remains visible."
    "athena-math-block" "\\begin{gather" "mathjax@3"))
(check (not (current-buffer)) "export selected a UI buffer")
(check (not (buffer-export source target "html"))
       "reusing a passive view required an active view")
(define image-output-dir (url-relative source "export"))
(system-mkdir image-output-dir)
(string-save "wrong destination asset" (url-append image-output-dir "asset.svg"))
(define image-target (url-append image-output-dir "site.html"))
(check (not (buffer-export source image-target "html")) "passive image export failed")
(check (equal? (string-load (url-append image-output-dir "site-1.svg")) image-bytes)
       "image export used the destination collision instead of its source document")
(check (string-contains? (string-load image-target) "width: 20px; height: 30px")
       "native image export lost CSS dimensions")
(cpp-buffer-close source)

;; Exercise the serializer directly, without input encoding conversions.
(check
  (equal? (native-html-finish-body
            '((h:p (h:img (@ (src "symbol.png"))) "word tail")) #t)
          '((h:p (h:span (@ (class "no-breaks"))
                   (h:img (@ (src "symbol.png"))) "word") " tail")))
  "CSS finalization skipped the first body node")
(check
  (equal? (native-html-concat-output
            '((h:a (@ (id "before"))) (h:h2 "Heading")
              (h:a (@ (id "after"))) "body"))
          '((h:h2 (@ (id "before")) (h:a (@ (id "after"))) "Heading") "body"))
  "heading consolidation lost an anchor or moved body content")
(check
  (equal? (native-html-concat-output
            '((h:a (@ (id "before"))) (h:h2 (@ (id "own")) "Heading")
              (h:a (@ (id "after")))))
          '((h:h2 (@ (id "own")) "Heading")
            (h:a (@ (id "before"))) (h:a (@ (id "after")))))
  "heading consolidation overwrote an existing destination")
(check
  (equal? (native-html-concat-output
            '((h:a (@ (id "left"))) (h:table (h:tr (h:td "Cell")))
              (h:a (@ (id "right")))))
          '((h:a (@ (id "left")))
            (h:table (@ (id "right")) (h:tr (h:td "Cell")))))
  "table consolidation changed adjacent-anchor priority")
(check
  (equal? (native-html-list-items
            '(document "preamble" (concat (item* "Term") "first")
               "continuation" (itemize (document (concat (item) "nested")))
               (concat (assign "item-nr" "2") (item) "second")))
          '(document (document "preamble")
             (!item (item* "Term")
               (document (concat "first") "continuation"
                 (itemize (document (concat (item) "nested")))))
             (!item (item) (document (concat "second")))))
  "native list grouping lost continuation paragraphs or consumed a nested list")
(check
  (equal? (native-html-normalize-document
            '(surround "before " " after"
               (with "color" "red" (document "first" "" "last"))) #t)
          '(document
             (concat "before " (with "color" "red" (concat "first")))
             (concat (with "color" "red" (concat)))
             (concat (with "color" "red" (concat "last")) " after")))
  "surround normalization lost a paragraph, style or boundary text")
(check
  (equal? (native-html-normalize-document
            '(surround "[" "]" (document "one")) #t)
          '(document (concat "[one]")))
  "single-paragraph surround lost block identity")
(check
  (equal? (native-html-finalize
            '((h:p (h:div (@ (h:custom "h:value")) "h:text"))
              (svg:svg (svg:path (@ (d "M0 0"))))) #f)
          '(*TOP* (div (@ (h:custom "h:value")) "h:text")
                   (svg:svg (svg:path (@ (d "M0 0"))))))
  "HTML finalization rewrote attributes/text or foreign namespace names")
(use-modules (convert html htmlout))
(use-modules (convert html html-export))
(let ((previous-source current-save-source)
      (previous-target current-save-target)
      (data (bytes-load (url-relative source "asset.svg"))))
  (dynamic-wind
    (lambda ()
      (set! current-save-source source)
      (set! current-save-target (url-relative source "embedded.html")))
    (lambda ()
      (for-each
        (lambda (identifier)
          (let ((output (serialize-html
                          (texmacs->html
                            `(image (tuple (raw-data ,data) ,identifier)
                                    "20px" "30px" "" "") '()))))
            (check (string-contains? output "embedded-1.svg")
                   "embedded web image was rasterized or lost its suffix")
            (check (equal? data (bytes-load (url-relative source "embedded-1.svg")))
                   "embedded image bytes changed across native conversion")))
        '("svg" "asset.svg")))
    (lambda ()
      (set! current-save-source previous-source)
      (set! current-save-target previous-target))))
(for-each
  (lambda (source)
    (catch #t
      (lambda ()
        (let* ((native (html->texmacs-tree (parse-html-snippet source)))
               (validated (catch #t
                            (lambda () (latex-export-prepare-v3-source native))
                            (lambda args (error "invalid before HTML export" args))))
               (html (convert native "texmacs-tree" "html-snippet")))
          (check (or (string-contains? html "\\(")
                     (string-contains? html "\\[")
                     (string-contains? html "athena-math-block"))
                 (string-append "HTML import/reexport lost mathematics: " html))))
      (lambda args (error "HTML mathematical roundtrip failed" source args))))
  '("<p>x<sub>i<sup>2</sup></sub></p>"
    "<math><msub><mi>y</mi><mn>1</mn></msub></math>"
    "<math><semantics><mrow><mi>x</mi><mo>+</mo><mn>1</mn></mrow><annotation encoding='application/x-tex'>x+1</annotation></semantics></math>"
    "<img class='tex' alt='z^3'>"
    "<script src='https://example.test/mathjax.js'></script><p>\\(x_i\\)</p>"))
(check
  (equal? (native-html-concat-plan
            '(concat "left" (htab "1fn" "first") "middle"
                     (htab "1fn" "last") "right"))
          '((!left "left") (!middle "middle") (!right "right")))
  "weak tab grouping lost its final alignment segment")
(check
  (equal? (native-html-paragraph
            '(concat (vspace* "1em") (vspace* "2px") "before" (vspace "3px"))
            '("before" (h:span (@ (style "color: red")) (h:p "inside")) "after"))
          '((h:p (@ (style "margin-top: max(1em, 2px)")) "before")
            (h:div (@ (style "color: red")) (h:p "inside"))
            (h:p (@ (style "margin-bottom: 3px")) "after")))
  "paragraph layout lost style, block boundaries or mixed-unit spacing")
(define table-plan
  (native-html-table-plan
    '(tformat
       (cwith "1" "1" "1" "1" "cell-background" "red")
       (cwith "1" "-1" "1" "-1" "cell-background" "blue")
       (cwith "1" "1" "1" "1" "cell-row-span" "2")
       (cwith "1" "1" "1" "1" "cell-col-span" "2")
       (table (row (cell "merged") (cell "hidden") (cell "top"))
              (row (cell "hidden") (cell "hidden")
                   (tformat (cwith "cell-background" "green") (cell "bottom")))))))
(check (equal? (cadr table-plan) '("merged" "top" "bottom"))
       "table spans changed cell conversion order or exposed covered cells")
(define table-output
  (native-html-table-output table-plan '(("merged") ("top") ("bottom"))))
(check (equal? (cadr (cadr (caddr (car table-output))))
               '(h:td (@ (style "background-color: blue")
                         (rowspan "2") (colspan "2")) "merged"))
       "table formatting order or merged-cell attributes were lost")
(check (equal? (cadr (caddr (caddr (car table-output))))
               '(h:td (@ (style "background-color: green")) "bottom"))
       "cell-local formatting did not override outer table formatting")
(define native-markup-html
  (serialize-html
    (texmacs->html
      '(document
         (section "Native heading")
         (html-class "second" (html-class "first" (strong "A & B")))
         (html-tag "aside" (em "Side content"))
         (table (row (cell "Standalone cell")))
         (itemize (document (concat (item) "First item")
                            (concat (item) "Second item")))
         (hlink "Local document" "other.ath?view=1#target")
         (hover-balloon "Hover" "Explanation")
         (with "ornament-hpadding" "2px" "ornament-color" "red"
           (ornament "Decorated"))
         (code (document " a--b " "``x''"))
         "After code -- normal text"
         (render-small-figure "unused" (strong "Figure 3")
           (ATHENA) (em "Caption"))
         (html-javascript "if (x < y) f('&');"))
      '(("texmacs->html:css" . "on")))))
(for-each
  (lambda (expected)
    (check (string-contains? native-markup-html expected)
           (string-append "native markup dispatch lost: " expected)))
  '("<h2>Native heading</h2>" "class=\"first second\""
    "A &amp; B" "<aside><em>Side content</em></aside>" "Standalone cell"
    "<ul>" "First item" "Second item" "other.html?view=1#target"
    "balloon-anchor" "Explanation" "padding-left: 2px" "padding-right: 2px"
    " a--b \n``x''" "After code – normal text"
    "figure-caption-label" "<strong>Figure 3</strong>" "<em>Caption</em>" "athena-logo"
    "if (x < y) f('&');"))
(check (catch #t
         (lambda ()
           (texmacs->html '(unsupported-html-fixture "must not disappear") '())
           #f)
         (lambda args #t))
       "unknown HTML markup was silently discarded")
(check (catch #t
         (lambda ()
           (texmacs->html '(specific "image" (math "x")) '())
           #f)
         (lambda args
           (string-contains? (object->string args) "raster fallback")))
       "plain inline mathematics bypassed the MathJax-only export boundary")
(define serialized (serialize-html `(*TOP* (html (body (p ,payload))))))
(check (string-contains? serialized (string #\x03c0 #\x2208 #\x2124))
       "HTML serialization changed Unicode characters")
(check (string-contains? serialized "followed by several words")
       "HTML serialization lost text after Unicode")

;; These boundaries changed ownership from Scheme to the native HTML5 DOM.
(check (equal? (parse-html-fragment "<p>A &amp; &lt; B</p>")
               '(*TOP* (p "A & < B")))
       "fragment parser changed entities or introduced document wrappers")
(define escaped
  (serialize-html '(*TOP* (p (@ (title "a\"b&c")) "<&amp;>"))))
(check (equal? (parse-html-fragment escaped)
               '(*TOP* (p (@ (title "a\"b&c")) "<&amp;>")))
       "HTML text or attributes were double-escaped")
(check (equal? (parse-html-fragment "<table><tr><td>x</table>")
               '(*TOP* (table (tbody (tr (td "x"))))))
       "HTML5 table construction was bypassed")
(check (equal? (parse-html-fragment
                 (serialize-html '(*TOP* (pre " a\n  b ")
                                        (script "if (a < b && c) f();")
                                        (style "p::after { content: '>'; }"))))
               '(*TOP* (pre " a\n  b ")
                       (script "if (a < b && c) f();")
                       (style "p::after { content: '>'; }")))
       "raw-text or preformatted contents changed")
(check (equal? (parse-html-fragment
                (serialize-html '(*TOP* (template (p "inside")))))
               '(*TOP* (template (p "inside"))))
       "template contents were lost")
(check (equal? (parse-html-fragment "<math><mi>x</mi></math>")
               '(*TOP* (math (@ (xmlns "http://www.w3.org/1998/Math/MathML"))
                            (mi "x"))))
       "HTML5 MathML namespace was lost")
(check (equal? (parse-html-fragment "<svg><foreignObject><p>x</p></foreignObject></svg>")
               '(*TOP* (svg (@ (xmlns "http://www.w3.org/2000/svg"))
                           (foreignObject
                             (p (@ (xmlns "http://www.w3.org/1999/xhtml")) "x")))))
       "foreign-content HTML integration lost its namespace")
(define mathjax-input
  "<!doctype html><html><head><script src='mathjax@3/tex-mml-chtml.js'></script></head><body><p>before \\(x&lt;y\\) after</p><code>$literal$</code></body></html>")
(define mathjax-parsed (parse-html mathjax-input))
(check (equal? mathjax-parsed
        '(*TOP* (*DOCTYPE* html)
          (html (head (script (@ (src "mathjax@3/tex-mml-chtml.js"))))
                (body (p "before " (mathjax "\\(x<y\\)") " after")
                      (code "$literal$")))))
       "MathJax text extraction lost payload or entered literal text")
(check (equal? mathjax-parsed (parse-html mathjax-input))
       "HTML parsing leaked per-conversion state")
(check (catch #t
         (lambda () (serialize-html '(*TOP* (br "lost"))) #f)
         (lambda args #t))
       "serializer silently discarded void-element contents")

;; Preservation output must never be passed through as mathematical HTML.
(check (catch #t
         (lambda () (native-html-math-output '(nonconverted "lost") #f #f) #f)
         (lambda args #t))
       "native mathematical export accepted lossy output")

;; Per-document resource plans must not retain settings from the previous file.
(define page-plan
  (native-html-document-plan
    '(with "html-title" "A & B" "html-site-version" "2"
           "html-extra-css" (quote (tuple "one.css" "two.css"))
           "html-head-javascript" "if (a < b) run();" (document "body"))
    '("generic") "p { color: red }" "---" "ATHENA" #t))
(define planned-html
  (serialize-html (native-html-document-output page-plan '("A & B") '((p "body")))))
(for-each
  (lambda (value) (check (string-contains? planned-html value)
                         (string-append "native document plan lost: " value)))
  '("A &amp; B" "one.css" "two.css" "if (a < b) run();" "mathjax@3"))
(define fresh-plan
  (native-html-document-plan '(document "body") '("generic") "" "---" "ATHENA" #f))
(check (and (not (cadr fresh-plan))
            (not (string-contains?
              (serialize-html (native-html-document-output fresh-plan '("Fresh") '()))
              "mathjax")))
       "native document plan leaked previous export settings")
