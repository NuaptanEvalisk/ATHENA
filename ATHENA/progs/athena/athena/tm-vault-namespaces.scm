(texmacs-module (athena athena tm-vault-namespaces)
  (:use (kernel boot abbrevs)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)))

(define-secure-symbols namespace-info-page
                       namespace-manager-show
                       namespace-explorer-show
                       open-namespace-manager
                       open-namespace-explorer)

(tm-define (open-namespace-manager)
  (:interactive #t)
  (namespace-manager-show))

(tm-define (open-namespace-explorer)
  (:interactive #t)
  (namespace-explorer-show))

(tmfs-load-handler (ns name)
  (tree->stree (namespace-info-page name)))
