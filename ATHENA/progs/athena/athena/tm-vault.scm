(texmacs-module (athena athena tm-vault)
  (:use (kernel boot abbrevs)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)))
(import-from (kernel athena tm-preferences))


(define-secure-symbols wikilink-repair-apply vault-transclude-repair
                       node-reference-jump-to-source
                       vault-jump-to-source artifact-jump-to-position
                       artifact-navigation-failed
                       load-buffer load-vault-dir
                       string->url vault-load-latest-action
                       vault-validate-root-namespace
                       go-to-system-welcome-page
                       go-to-welcome-page
                       go-to-vault-initial-page
                       load-help-article
                       load-help-buffer
                       vault-show-explorer
                       vault-show-explorer-and-track
                       vault-explorer-track-file
                       open-namespace-manager
                       open-namespace-explorer
                       namespace-new-file-within-wizard
                       namespace-info-page
                       open-quick-switcher
                       new-document)

(tmfs-load-handler (wikilink-repair name)
  (tree->stree (wikilink-repair-page name)))

(tmfs-load-handler (ns name)
  (tree->stree (namespace-info-page name)))

(tmfs-load-handler (welcome name)
  (tree->stree (vault-welcome-page)))

(tmfs-load-handler (artifact name)
  (if (artifact-open-uuid name)
      `(document
         (style (tuple "generic"))
         (body (document "Opening artifact...")))
      `(document
         (style (tuple "generic"))
         (body (document (bold "Artifact not found: ") ,name)))))

(tmfs-title-handler (artifact name doc)
  "Artifact")

(tmfs-permission-handler (artifact name kind)
  (== kind "read"))

(tmfs-load-handler (artifact-disambiguation name)
  (tree->stree (artifact-disambiguation-page name)))

(tmfs-title-handler (artifact-disambiguation name doc)
  "Artifact disambiguation")

(tmfs-permission-handler (artifact-disambiguation name kind)
  (== kind "read"))

(tmfs-load-handler (artifact-name-query name)
  (tree->stree (artifact-name-query-page name)))

(tmfs-title-handler (artifact-name-query name doc)
  "Artifact disambiguation")

(tmfs-permission-handler (artifact-name-query name kind)
  (== kind "read"))

;; Native callbacks retain the kernel preference registry's initialization order.
(register-preference-callback-procedures
  (list notify-cursor-color notify-document-background-color notify-enunciation-color notify-focus-border-width notify-focus-color notify-labels-mode notify-link-color notify-selection-color notify-vault-explorer-track notify-vault-preferences-mode))
