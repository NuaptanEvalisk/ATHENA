(texmacs-module (athena athena tm-vault-namespaces)
  (:use (kernel boot abbrevs)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)))

(define-secure-symbols namespace-info-page
                       namespace-manager-show
                       open-namespace-manager)

(tm-define (open-namespace-manager)
  (:interactive #t)
  (namespace-manager-show))

(tmfs-load-handler (ns name)
  (namespace-info-page name))
