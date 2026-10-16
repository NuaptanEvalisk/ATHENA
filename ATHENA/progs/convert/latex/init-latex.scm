
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-latex.scm
;; DESCRIPTION : setup latex converters
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert latex init-latex))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; LaTeX format
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (latex-recognizes-at? s pos)
  (set! pos (format-skip-spaces s pos))
  (cond ((format-test? s pos "\\document") #t)
        ((format-test? s pos "\\usepackage") #t)
        ((format-test? s pos "\\input") #t)
        ((format-test? s pos "\\includeonly") #t)
        ((format-test? s pos "\\chapter") #t)
        ((format-test? s pos "\\appendix") #t)
        ((format-test? s pos "\\section") #t)
        ((format-test? s pos "\\begin") #t)
        (else #f)))

(define (latex-recognizes? s)
  (and (string? s) (latex-recognizes-at? s 0)))

(define-format latex
  (:name "LaTeX")
  (:suffix "tex")
  (:recognize latex-recognizes?))

(define-format latex-class
  (:name "LaTeX class")
  (:suffix "ltx" "sty" "cls"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; TeXmacs -> LaTeX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(lazy-define (convert latex tmtex) texmacs->latex)

(tm-define (latex-native-v3-math? source)
  (and (pair? source)
       (or (and (symbol? (car source))
                (memq (car source)
                  '(math-block math-grid scripts math-accent math-annotation
                    math-long-arrow math-middle math-symbol math-variant
                    delimited big-operator)))
           (list-any latex-native-v3-math? (cdr source)))))

(define (latex-native-v3-check-output output)
  (cond ((not output)
         (error "XML v3 LaTeX export failed: converter returned no output"))
        ((pair? output)
         (when (memq (car output)
                 '(nonconverted !athena-data-inline athena-preserved-object))
           (error "Unsupported XML v3 LaTeX export: preservation or nonconverted placeholder"
                  output))
         (for-each latex-native-v3-check-output output))))

(tm-define (texmacs->latex-checked x opts)
  (let* ((source (if (tree? x) (latex-export-prepare-v3-source x) x))
         (output (texmacs->latex source opts)))
    (when (latex-native-v3-math? source)
      (latex-native-v3-check-output output))
    output))

(converter texmacs-stree latex-stree
  (:function-with-options texmacs->latex-checked)
  (:option "texmacs->latex:source-tracking" "off")
  (:option "texmacs->latex:conservative" "on")
  (:option "texmacs->latex:transparent-source-tracking" "on")
  (:option "texmacs->latex:attach-tracking-info" "on")
  (:option "texmacs->latex:replace-style" "on")
  (:option "texmacs->latex:expand-macros" "on")
  (:option "texmacs->latex:expand-user-macros" "off")
  (:option "texmacs->latex:use-macros" "on")
  (:option "texmacs->latex:encoding" "utf-8")
  (:option "texmacs->latex:portable" "off"))

;; Selection exports must not traverse the metadata-dropping tree->stree edge.
;; Reuse the existing edge's option defaults and user preferences.
(tm-define (texmacs-tree->latex-stree x opts)
  (texmacs->latex-checked x
    (append opts (std-converter-options "texmacs-stree" "latex-stree"))))

(converter texmacs-tree latex-stree
  (:function-with-options texmacs-tree->latex-stree))

(converter latex-stree latex-document
  (:function serialize-latex))

(converter latex-stree latex-snippet
  (:function serialize-latex))

(tm-define (texmacs->latex-document x opts)
  ;; Keep typed XML v3 math attributes until the native exporter has decoded
  ;; them. A generic tm->stree pass cannot carry five-slot script placement,
  ;; MathGrid kind or MathBlock numbering; silently losing those is invalid.
  (serialize-latex (texmacs->latex-checked x opts)))

(tm-define (texmacs-stree->latex-document x opts)
  (if (latex-native-v3-math? x)
      (serialize-latex (texmacs->latex-checked x opts))
      (conservative-texmacs->latex x opts)))

(tm-define (texmacs-tree->latex-document x opts)
  (let ((prepared (latex-export-prepare-v3-source x)))
    (texmacs-stree->latex-document prepared opts)))

;; Keep the native tree until presentation metadata has been decoded. The
;; stree route remains available for legacy documents and conservative export.
(converter texmacs-tree latex-document
  (:function-with-options texmacs-tree->latex-document)
  (:option "texmacs->latex:source-tracking" "off")
  (:option "texmacs->latex:conservative" "on")
  (:option "texmacs->latex:transparent-source-tracking" "on")
  (:option "texmacs->latex:attach-tracking-info" "on")
  (:option "texmacs->latex:replace-style" "on")
  (:option "texmacs->latex:expand-macros" "on")
  (:option "texmacs->latex:expand-user-macros" "off")
  (:option "texmacs->latex:use-macros" "on")
  (:option "texmacs->latex:encoding" "utf-8")
  (:option "texmacs->latex:portable" "off"))

(converter texmacs-stree latex-document
  (:function-with-options texmacs-stree->latex-document)
  ;;(:function-with-options tracked-texmacs->latex)
  (:option "texmacs->latex:source-tracking" "off")
  (:option "texmacs->latex:conservative" "on")
  (:option "texmacs->latex:transparent-source-tracking" "on")
  (:option "texmacs->latex:attach-tracking-info" "on")
  (:option "texmacs->latex:replace-style" "on")
  (:option "texmacs->latex:expand-macros" "on")
  (:option "texmacs->latex:expand-user-macros" "off")
  (:option "texmacs->latex:use-macros" "on")
  (:option "texmacs->latex:encoding" "utf-8")
  (:option "texmacs->latex:portable" "off"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; LaTeX -> TeXmacs
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (latex-document->texmacs x . opts)
  (latex-native-document->texmacs
   x
   (== (get-preference "latex->texmacs:fallback-on-pictures") "on")))

(converter latex-document latex-tree
  (:function parse-latex-document))

(converter latex-snippet latex-tree
  (:function parse-latex))

(converter latex-document texmacs-tree
  (:function-with-options latex-document->texmacs)
  (:option "latex->texmacs:fallback-on-pictures" "on")
  (:option "latex->texmacs:source-tracking" "off")
  (:option "latex->texmacs:conservative" "off")
  (:option "latex->texmacs:transparent-source-tracking" "off"))

(converter latex-class-document texmacs-tree
  (:function latex-class-document->texmacs))

(converter latex-tree texmacs-tree
  (:function latex->texmacs))
