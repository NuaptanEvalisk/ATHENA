;; Load in the centralized ATHENA runtime test process. No source documents,
;; buffers, vaults or files are read or written by this test.
(use-modules (convert latex init-latex) (convert html init-html)
             (convert html html-export) (convert rewrite tmtm-brackets))
(define (check value message)
  (unless value (error "Native v3 export regression" message)))
(define (expect-error thunk reason)
  (let ((caught (catch #t (lambda () (thunk) #f) (lambda args args))))
    (check (and caught (string-contains? (object->string caught) reason))
           (string-append "missing explicit failure: " reason))))
(define (property! tree key value)
  (check (eq? (car (tree-update-node-properties!
                    tree (list (list key value)) '())) 'ok)
         "could not construct typed native fixture"))
(define (clean? text)
  (and (string? text)
       (not (string-contains? text "nonconverted"))
       (not (string-contains? text "athena-data"))))

(define scripts (stree->tree '(scripts "x" "" "" "i" "n")))
(property! scripts "math:scripts-policy" '(string "o"))
(define native (tree-rebuild (stree->tree '(math)) (list scripts)))
(define before (tree-node-properties scripts))
(define options '(("texmacs->html:mathjax" . "on")))

;; Exercise the real registered routes used for documents and clipboard output.
(for-each
  (lambda (target)
    (let ((output (convert native "texmacs-tree" target)))
      (check (clean? output) "LaTeX route returned preservation output")))
  '("latex-document" "latex-snippet"))
(for-each
  (lambda (target)
    (expect-error
      (lambda () (convert native "texmacs-tree" target
                   '("texmacs->html:mathjax" . "off")
                   '("texmacs->html:images" . "on")))
      "image fallback is disabled")
    (check (clean? (apply convert native "texmacs-tree" target options))
           "MathJax route returned preservation output"))
  '("html-document" "html-snippet"))
(check (equal? before (tree-node-properties scripts))
       "export mutated typed script metadata")

;; These are exactly the fixed-arity v3 AST shapes now inserted by the
;; keyboard and Math toolbar's shared arrow/operator action. Exercise the real
;; LaTeX route as an integration check, independent of the editor QTest build.
(check (string-contains?
         (texmacs->latex-document '(math (math-long-arrow "→" "i" "n")) '())
         "\\xrightarrow[i]{n}")
       "native long-arrow lower/upper slots were transposed during export")
(check (string-contains?
         (texmacs->latex-document '(math (math-long-arrow "→" "" "")) '())
         "\\xrightarrow{}")
       "empty native long-arrow annotation slots were lost")
(check (string-contains?
         (texmacs->latex-document '(math (big-operator "∑" "0")) '())
         "\\sum")
       "native big operator glyph failed the real export route")
(for-each
  (lambda (kind command)
    (check (string-contains?
             (texmacs->latex-document `(math (math-variant ,kind "A")) '())
             command)
           "native math alphabet variant failed the real export route"))
  '("bf" "bb" "cal")
  '("\\mathbf{A}" "\\mathbb{A}" "\\mathcal{A}"))
(for-each
  (lambda (kind command)
    (check (string-contains?
             (texmacs->latex-document `(math (math-variant ,kind "A")) '())
             command)
           "mathematical alphabet export changed its original font semantics"))
  '("mbf" "mbfu" "frak")
  '("\\boldsymbol{A}" "\\mathbf{A}" "\\mathfrak{A}"))
;; Verify dependencies as well as visible commands. Bare LaTeX does not
;; define boldsymbol/mathfrak; these are real package requirements, not
;; cosmetic formatting differences.
(texmacs->latex-document '(math (math-variant "mbf" "A")) '())
(check (member "amsmath" (latex-export-latex-extra-packages))
       "mathematical bold export omitted its amsmath dependency")
(texmacs->latex-document '(math (math-variant "frak" "A")) '())
(check (member "amsfonts" (latex-export-latex-extra-packages))
       "Fraktur export omitted its amsfonts dependency")

(define block (stree->tree '(math-block "x")))
(property! block "math:block-numbered" '(boolean #t))
(define block-source (latex-export-prepare-v3-source block))
(define numbered
  (texmacs->html-document
    `(document (style "generic") (body (document ,block-source))) options))
(check (string-contains? numbered "\\begin{equation}")
       "numbered MathBlock lost its equation environment")
(let ((config (string-contains numbered "window.MathJax.tex.tags = 'ams'"))
      (loader (string-contains numbered "npm/mathjax@3/")))
  (check (and config loader (< config loader))
         "AMS numbering configuration must precede the loader"))
(check (not (string-contains? numbered "\\[\\begin{equation}"))
       "MathBlock acquired nested MathJax display delimiters")

;; Preservation output nested under a supported native node must fail too.
(define unsupported
  '(math (scripts "o" (unknown "cannot export") "" "" "i" "n")))
(expect-error (lambda () (texmacs-stree->latex-document unsupported '()))
              "placeholder")
(expect-error (lambda () (texmacs->html-document unsupported options))
              "placeholder")
(expect-error
  (lambda () (texmacs->html-document `(unknown-envelope ,block-source) options))
  "unknown enclosing markup")

;; A supported native node with an unsupported upright integral must raise
;; misc-error at the generated binding, retaining the original native reason.
(define rejected-native
  `(math (big-operator ,(string #\x222b) "1")))
(define (expect-native-error thunk reason)
  (let ((caught (catch 'misc-error (lambda () (thunk) #f)
                  (lambda args args))))
    (check (and caught (string-contains? (object->string caught) reason))
           "native exception lost its Scheme error or diagnostic")))

;; Entering at depth zero clears placeholders; entering within an active
;; parent export must preserve them. This observes depth without a test API.
(define (check-export-depth active?)
  (let ((marker (tmtex-athena-object-placeholder '(unknown "depth sentinel"))))
    (check (string? marker) "placeholder probe did not return its marker")
    (dynamic-wind
      latex-export-enter
      (lambda ()
        (check (equal? (not (not (tmtex-athena-object-placeholder-ref marker)))
                       active?)
               "export depth was not restored exactly once"))
      latex-export-leave)))
(do ((attempt 0 (+ attempt 1))) ((= attempt 3))
  (expect-native-error
    (lambda () (texmacs->latex-document rejected-native '())) "big-operator")
  (check-export-depth #f)
  (check (clean? (texmacs->latex-document native '()))
         "successful export after failure did not recover")
  (check-export-depth #f))
(expect-native-error
  (lambda () (latex-export-prepare-v3-source
               (stree->tree '(math-block "missing typed metadata"))))
  "malformed")

;; The strict native math validator is also the safety boundary for the XML
;; v3 reader/writer. A MathLongArrow containing a malformed MathSymbol must
;; fail closed, not dereference a non-atomic semantic ID and crash ATHENA.
(define malformed-arrow
  (stree->tree '(math-long-arrow
                   (math-symbol (concat "differential-d")) "" "")))
(expect-native-error
  (lambda () (latex-export-prepare-v3-source malformed-arrow))
  "malformed")
(check (clean? (texmacs->latex-document native '()))
       "failed malformed-arrow validation poisoned a later native export")

;; The compact XML v3 codec represents these physical descriptors as parent
;; XML attributes, with no place to store their own source IDs/properties.
;; They must fail closed instead of silently losing the identified child.
(define (reject-attributed-descriptor source position)
  (let ((child (tree-ref source position)))
    (check (eq? (car (tree-ensure-node-id! child)) 'ok)
           "could not construct identified descriptor")
    (expect-native-error
      (lambda () (latex-export-prepare-v3-source source)) "malformed")))
(reject-attributed-descriptor (stree->tree '(big-operator "∑" "0")) 0)
(reject-attributed-descriptor (stree->tree '(big-operator "∑" "0")) 1)
(reject-attributed-descriptor (stree->tree '(math-middle "|")) 0)
(do ((slot 0 (+ slot 1))) ((= slot 5))
  (reject-attributed-descriptor
    (stree->tree '(delimited "(" ")" "m" "1" "2" "x")) slot))
(check (clean? (texmacs->latex-document native '()))
       "descriptor metadata rejection damaged a later native export")

;; The native MathGrid model must reject an unknown cell height mode before
;; exporting or persisting it. The typesetter only implements auto/exact/min/max;
;; accepting arbitrary strings would silently ignore an intended constraint.
(define (height-mode-grid mode)
  (let ((cell (stree->tree '(cell "x"))))
    (property! cell "math:cell-format"
               (list 'dictionary (list (list "height-mode" (list 'string mode)))))
    (let ((grid (tree-rebuild
                  (stree->tree '(math-grid))
                  (list (tree-rebuild (stree->tree '(row)) (list cell))))))
      (property! grid "math:grid-kind" '(string "matrix"))
      grid)))
(expect-native-error
  (lambda () (latex-export-prepare-v3-source (height-mode-grid "invented")))
  "malformed")
;; Supported modes are valid native trees even though LaTeX currently rejects
;; this cell-format property rather than silently dropping it during export.
(for-each
  (lambda (mode)
    (expect-native-error
      (lambda () (latex-export-prepare-v3-source (height-mode-grid mode)))
      "Unsupported MathGrid cell formatting"))
  '("auto" "exact" "min" "max"))

;; Exercise the real Scheme callback boundary inside a running C++ export.
;; Restore the callback before recursion; no test binding is installed in C++.
(define callback-slot (module-variable (current-module) 'tmtm-match-brackets))
(check callback-slot "export callback was not imported")
(define saved-callback (variable-ref callback-slot))
(define callback-ran? #f)
(define nested-checked? #f)
(dynamic-wind
  (lambda ()
    (variable-set! callback-slot
      (lambda (source)
        (variable-set! callback-slot saved-callback)
        (set! callback-ran? #t)
        (expect-native-error
          (lambda () (texmacs->latex-document rejected-native '()))
          "big-operator")
        (check-export-depth #t)
        (set! nested-checked? #t)
        (saved-callback source))))
  (lambda () (texmacs->latex-document native '()))
  (lambda () (variable-set! callback-slot saved-callback)))
(check callback-ran? "nested native failure did not reach the callback")
(check nested-checked? "nested failure assertions did not complete")
(check-export-depth #f)

;; A continuation escape bypasses C++ destructors. The driver dynwind handler
;; must still leave once, and the abandoned stack callback cannot be reentered.
(define abandoned-continuation #f)
(define continuation-stage 'capture)
;; Guile unwinds the invoking context before validating the target frames.
;; A catch installed only around reentry would already be gone when Guile
;; rejects it. Keep the handler in the common ancestor of both continuations.
(define rejected-reentry
  (dynamic-wind
    (lambda () #t)
    (lambda ()
      (catch 'misc-error
        (lambda ()
          (let ((escaped
                  (call-with-current-continuation
                    (lambda (escape)
                      (dynamic-wind
                        (lambda ()
                          (variable-set! callback-slot
                            (lambda (source)
                              (call-with-current-continuation
                                (lambda (inside)
                                  (set! abandoned-continuation inside)
                                  (escape 'escaped-native-export))))))
                        (lambda () (texmacs->latex-document native '()))
                        (lambda ()
                          (variable-set! callback-slot saved-callback)))))))
            (check (eq? escaped 'escaped-native-export)
                   "native export did not escape")
            (check-export-depth #f)
            (set! continuation-stage 'reentry)
            (abandoned-continuation 'resume)
            #f))
        (lambda args args)))
    (lambda () (variable-set! callback-slot saved-callback))))
(check (and (eq? continuation-stage 'reentry)
            rejected-reentry
            (equal? (cadr rejected-reentry) "scm_dynstack_wind_1")
            (string-contains? (object->string rejected-reentry)
                              "cannot invoke continuation"))
       "abandoned native continuation was not rejected at the frame boundary")
(check-export-depth #f)
(check (clean? (texmacs->latex-document native '()))
       "export did not recover after rejected continuation reentry")
(check-export-depth #f)
(display "PASS: native v3 runtime export routes and numbered MathJax configuration\n")
