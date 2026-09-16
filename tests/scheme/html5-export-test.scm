(define (check condition message)
  (unless condition (error message)))

(define (contains-symbol? x symbol)
  (or (eq? x symbol)
      (and (pair? x) (or (contains-symbol? (car x) symbol)
                         (contains-symbol? (cdr x) symbol)))))

(define mathml-inline
  (convert
   "<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mfrac><msup><mi>x</mi><mn>2</mn></msup><mrow><mi>y</mi><mo>+</mo><mn>1</mn></mrow></mfrac></math>"
   "html-snippet" "texmacs-stree"))
(check (contains-symbol? mathml-inline 'frac)
       "HTML import lost the native MathML fraction")
(check (contains-symbol? mathml-inline 'rsup)
       "HTML import lost the native MathML superscript")

(define mathml-table
  (convert
   "<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><mtable columnalign=\"left right\"><mtr><mtd><mi>a</mi></mtd><mtd><mi>b</mi></mtd></mtr></mtable></math>"
   "html-snippet" "texmacs-stree"))
(check (contains-symbol? mathml-table 'tabular)
       "HTML import lost the native MathML table")

(define mathml-display
  (convert
   "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><mi>x</mi></math>"
   "html-snippet" "texmacs-stree"))
(check (contains-symbol? mathml-display 'equation*)
       "HTML import lost MathML display mode")

(set-preference "mathml->texmacs:latex-annotations" "on")
(define mathml-annotation
  (convert
   "<math xmlns=\"http://www.w3.org/1998/Math/MathML\"><semantics><mi>x</mi><annotation encoding=\"application/x-tex\">x^2</annotation></semantics></math>"
   "html-snippet" "texmacs-stree"))
(check (contains-symbol? mathml-annotation 'rsup)
       "HTML import ignored the MathML TeX annotation")

(init-style "generic")
(buffer-set-body
 (current-buffer)
 (stree->tree
  '(document
    (concat "HTML5 line one" (new-line) "line two")
    (equation* (math "x+1")))))
(update-current-buffer)
(update-forced)

(set-preference "texmacs->html:mathjax" "on")
(set-preference "texmacs->html:images" "off")

(define out (string->url "/tmp/athena-html5-export-test.html"))
(when (buffer-export (current-buffer) out "html")
  (error "HTML export failed"))
(define html (string-load out))

(check (string-starts? html "<!DOCTYPE html>")
       "HTML export does not start with the HTML5 doctype")
(check (not (string-contains? html "<?xml"))
       "HTML export still contains an XML declaration")
(check (not (string-contains? html "XHTML"))
       "HTML export still advertises XHTML")
(check (not (string-contains? html "xmlns="))
       "HTML export still contains an XHTML namespace")
(check (not (string-contains? html "MathML"))
       "HTML export still contains MathML output")
(check (string-contains? html "<meta charset=\"utf-8\">")
       "HTML export is missing a charset meta element")
(check (not (string-contains? html "</meta>"))
       "HTML void meta element has an XML-style closing tag")
(check (string-contains? html "<br>")
       "HTML line break was not serialized as an HTML5 void element")
(check (not (string-contains? html "<br />"))
       "HTML line break still uses XHTML self-closing syntax")
(check (string-contains? html "cdn.jsdelivr.net/npm/mathjax@3")
       "MathJax export did not include the MathJax runtime")
(check (or (string-contains? html "\\(")
           (string-contains? html "\\["))
       "Math formula was not exported as MathJax input")

(print-to-file
 (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
