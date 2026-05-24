(texmacs-module (athena athena tm-vault-namespaces)
  (:use (kernel boot abbrevs)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)))

(define-secure-symbols namespace-info-page
                       namespace-manager-show
                       namespace-explorer-show
                       namespace-new-file-wizard
                       namespace-create-file-with-optional-initializer
                       open-namespace-manager
                       open-namespace-explorer
                       namespace-new-file-within-wizard)

(tm-define (open-namespace-manager)
  (:interactive #t)
  (namespace-manager-show))

(tm-define (open-namespace-explorer)
  (:interactive #t)
  (namespace-explorer-show))

(tm-define (namespace-new-file-within-wizard)
  (:interactive #t)
  (let ((path (namespace-new-file-wizard)))
    (if (and (string? path) (!= path ""))
        (load-buffer (string->url path)))))

(tmfs-load-handler (ns name)
  (tree->stree (namespace-info-page name)))
