(texmacs-module (athena athena tm-vault-welcome)
  (:use (kernel boot abbrevs)
        (athena athena tm-vault-recents)
        (athena menus file-menu)))

(tm-define (vault-load-latest-action path-s)
  (load-vault-dir (string->url path-s)))

(tm-define (go-to-welcome-page)
  (load-buffer "tmfs://welcome/home"))

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
