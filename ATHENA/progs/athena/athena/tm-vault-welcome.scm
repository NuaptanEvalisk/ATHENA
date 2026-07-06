(texmacs-module (athena athena tm-vault-welcome)
  (:use (kernel boot abbrevs)
        (athena athena tm-vault-recents)
        (athena menus file-menu)))

(tm-define (vault-load-latest-action path-s)
  (load-vault-dir (string->url path-s)))

(define (vault-welcome-default-page)
  "tmfs://welcome/home")

(define (vault-welcome-field data index default)
  (if (and (list? data)
           (> (length data) index)
           (string? (list-ref data index)))
      (list-ref data index)
      default))

(define (vault-welcome-startup-page data)
  (vault-welcome-field data 4 ""))

(define (vault-welcome-one-time-startup-page data)
  (vault-welcome-field data 5 ""))

(define (vault-welcome-maintenance-summary-path data)
  (vault-welcome-field data 6 ""))

(define (vault-welcome-rag-index-path data)
  (vault-welcome-field data 7 "rag.sqlite"))

(define (vault-welcome-websites-path data)
  (vault-welcome-field data 8 "websites.json"))

(define (vault-welcome-normalized data)
  (list (vault-welcome-field data 0 "")
        (vault-welcome-field data 1 "map.tmdb")
        (vault-welcome-field data 2 "")
        (vault-welcome-field data 3 "ns.sqlite")
        (vault-welcome-startup-page data)
        (vault-welcome-one-time-startup-page data)
        (vault-welcome-maintenance-summary-path data)
        (vault-welcome-rag-index-path data)
        (vault-welcome-websites-path data)
        (vault-welcome-field data 9 "")))

(define (vault-welcome-clear-one-time data)
  (let ((normalized (vault-welcome-normalized data)))
    (list (list-ref normalized 0)
          (list-ref normalized 1)
          (list-ref normalized 2)
          (list-ref normalized 3)
          (list-ref normalized 4)
          ""
          (list-ref normalized 6)
          (list-ref normalized 7)
          (list-ref normalized 8)
          (list-ref normalized 9))))

(define (vault-welcome-read-vaultfile)
  (if (and (defined? 'vault-active?) (vault-active?)
           (vaultfile-present? (vault-get-root)))
      (vaultfile-read (vault-get-root))
      '()))

(define (vault-welcome-target->buffer target)
  (cond ((string-null? target) (vault-welcome-default-page))
        ((or (string-starts? target "tmfs://")
             (string-starts? target "file://"))
         target)
        ((and (defined? 'vault-active?) (vault-active?))
         (url-append (vault-get-root) target))
        (else target)))

(define (vault-welcome-load-target target)
  (load-buffer (vault-welcome-target->buffer target)))

(tm-define (go-to-welcome-page)
  (let* ((data (vault-welcome-read-vaultfile))
         (startup-page (vault-welcome-startup-page data)))
    (vault-welcome-load-target
     (if (string-null? startup-page)
         (vault-welcome-default-page)
         startup-page))))

(tm-define (go-to-vault-initial-page)
  (let* ((data (vault-welcome-read-vaultfile))
         (one-time-page (vault-welcome-one-time-startup-page data)))
    (if (string-null? one-time-page)
        (go-to-welcome-page)
        (begin
          (if (and (defined? 'vault-active?) (vault-active?))
              (vaultfile-write (vault-get-root)
                               (vault-welcome-clear-one-time data)))
          (vault-welcome-load-target one-time-page)))))

(tmfs-load-handler (welcome name)
  (let* ((recent-files (recent-file-list 15))
         (recent-vaults (get-recent-vaults))
         (latest-vault (if (pair? recent-vaults) (car recent-vaults) #f)))
    (tm->stree
      `(document
         (TeXmacs ,(texmacs-compat-version))
         (style (tuple "generic"))
         (body (document
           (with "par-mode" "center"
             (document
               (with "font-size" "2" "font-series" "bold"
                 (concat "Welcome to " (ATHENA)))
               (with "font-size" "1.2" "font-shape" "italic"
                 "Advanced Typesetting and Hypertext Environment for Notes and Archives")))
           (vspace "2fn")

           (section* "Quick Start")
           (enumerate (document
             (concat (item) (action "Open Blank Buffer" "(new-document)"))
             ,@(if latest-vault
                   `((concat (item) (action ,(string-append "Load Latest Vault (" (url->system (url-tail (string->url latest-vault))) ")")
                                            ,(string-append "(vault-load-latest-action " (object->string latest-vault) ")"))))
                   '())))

           (vspace "1fn")
           (section* "Recent Files")
           (enumerate (document
             ,@(map (lambda (u)
                    `(concat (item) (action ,(url->system u) ,(string-append "(load-buffer " (object->string (url->string u)) ")"))))
                  recent-files)))

           (vspace "2fn")
           (with "font-size" "0.8" "color" "grey"
             (document
               (with "par-mode" "center"
                 (document
                   "ATHENA is a fork based on GNU TeXmacs."
                   (concat "Copyright " (copyright) " 1999-2026 Joris van der Hoeven and others.")
                   (concat "Copyright " (copyright) " 2026 Nuaptan F. Evalisk.")
                   "Released under the GNU General Public License version 3 or later."))))
           ))
         (initial (collection
           (associate "font-base-size" "7")
           (associate "font-family" "tt")
           (associate "page-medium" "automatic")))))))
