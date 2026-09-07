(import-from (generic document-edit))

(init-style "generic")

(define (check condition message . details)
  (unless condition
    (apply error (cons "Font alias regression" (cons message details)))))

(define (reset-font-init)
  (init-default "font")
  (init-default "math-font")
  (init-default "font-family"))

(define (check-family aliases expected-profile)
  (for-each
    (lambda (alias)
      (reset-font-init)
      (init-font alias)
      (check (== (get-init "font") expected-profile)
             "font alias did not select its smart-font profile"
             alias (get-init "font") expected-profile)
      (check (not (init-has? "math-font"))
             "font alias left a standalone math-font override" alias)
      (check (not (list-any (lambda (p) (string-ends? p "-font"))
                            (get-style-list)))
             "font alias left a legacy font package" alias))
    aliases))

(check-family '("Bonum" "bonum" "TeX Gyre Bonum") "TeX Gyre Bonum")
(check-family '("Pagella" "pagella" "TeX Gyre Pagella") "TeX Gyre Pagella")
(check-family '("Schola" "schola" "TeX Gyre Schola") "TeX Gyre Schola")
(check-family '("Termes" "termes" "TeX Gyre Termes") "TeX Gyre Termes")

;; The built-in short document menu passes the legacy matching math font as
;; an explicit argument.  It must still use the smart profile rather than
;; reinstalling pagella-font.ts.
(reset-font-init)
(init-font "pagella" "math-pagella")
(check (== (get-init "font") "TeX Gyre Pagella")
       "built-in Pagella menu did not use the smart-font profile")
(check (not (init-has? "math-font"))
       "built-in Pagella menu left the legacy math-font override")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
