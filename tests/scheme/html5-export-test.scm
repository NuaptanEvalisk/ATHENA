(define (check condition message)
  (unless condition (error message)))

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
