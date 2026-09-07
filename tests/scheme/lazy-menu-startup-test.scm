(define (check condition message)
  (unless condition (error "Lazy menu startup regression" message)))

(define (menu-label-present? menu label)
  (cond ((null? menu) #f)
        ((pair? menu)
         (or (and (string? (car menu)) (string=? (car menu) label))
             (menu-label-present? (car menu) label)
             (menu-label-present? (cdr menu) label)))
        (else #f)))

;; Normal GUI startup calls this barrier before opening the first editor
;; window.  Exercise that result explicitly in this headless test.
(lazy-menu-force-all)

(check (member '(generic embedded-menu)
               (%athena-definition-modules 'focus-misc-menu))
       "embedded image menu was not loaded by the startup menu barrier")

(define image (stree->tree '(image "probe.png" "" "" "" "")))
(define image-menu (focus-misc-menu image))
(check (menu-label-present? image-menu "Remove background")
       "linked image menu lost Remove background")
(check (menu-label-present? image-menu "Embed image")
       "linked image menu lost Embed image")

;; The startup barrier must leave the complete focus dispatch chain safe to
;; evaluate before mode-specific keyboard/edit paths have happened to load
;; helper modules. This catches hidden menu -> edit dependencies.
(focus-tag-menu image)

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file
  (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
