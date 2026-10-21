;; Native generic document style commands execute on the owning BufferActor
;; while preserving the public Scheme style extension points.
(import-from (generic document-style))
(init-style "generic")

(define (check condition message . details)
  (unless condition
    (apply error (cons message details))))

(for-each
  (lambda (key)
    (check (not (style-get-documentation key))
           "unevaluated menu arguments have no style documentation" key))
  '(theme generic (car styles) #f 42))
(let ((action (lambda () (add-style-package theme))))
  (set-procedure-property! action 'source '(lambda () (add-style-package theme)))
  (let* ((source (promise-source action))
         (balloon (property (car source) :balloon)))
    (check (not (apply (car balloon) (cdr source)))
           "menu balloon property accepts an unevaluated style argument")))
(let* ((style-name "generic.ats")
       (legacy (url-resolve
                 (url-or (url-relative (current-buffer) style-name)
                         (url-append "$ATHENA_STYLE_PATH" style-name))
                 "r")))
  (check (equal? (url-resolve-package "generic") legacy)
         "native style package resolver preserves local-before-global policy"))

;; Later category specializations remain authoritative for the native
;; style-overrides? and style-precedes? wrappers.
(tm-define (style-category p)
  (:require (in? p '("native-cat-a" "native-cat-b")))
  :native-test-category)
(check (style-overrides? "native-cat-a" "native-cat-b")
       "native style-overrides dispatches through Scheme style-category")

(tm-define (style-category-precedes? p q)
  (:require (and (== p :native-test-category)
                 (== q :native-later-category)))
  #t)
(tm-define (style-category p)
  (:require (== p "native-later"))
  :native-later-category)
(check (style-precedes? "native-cat-a" "native-later")
       "native style-precedes dispatches through Scheme category policy")

(set-style-list '("generic" "native-cat-a" "native-cat-b"))
(check (equal? (get-style-list) '("generic" "native-cat-b"))
       "native normalization drops package overridden by a later category peer")
(set-style-list '("generic" "native-later" "native-cat-a"))
(check (equal? (get-style-list) '("generic" "native-cat-a" "native-later"))
       "native normalization preserves legacy precedence reordering")

;; Included-package detection remains extensible and is evaluated by the
;; native has-style-package? command against the current normalized list.
(tm-define (style-includes? p q)
  (:require (and (== p "native-bundle") (== q "native-included")))
  #t)
(set-style-list '("generic" "native-bundle" "native-included"))
(check (equal? (get-style-list) '("generic" "native-bundle"))
       "native normalization removes package included by an earlier package")
(set-style-list '("generic" "native-bundle"))
(check (has-style-package? "native-included")
       "native package predicate reaches Scheme style-includes specialization")
(check (not (not-has-style-package? "native-included"))
       "native negated package predicate matches included package")

(set-style-list '("generic"))
(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
