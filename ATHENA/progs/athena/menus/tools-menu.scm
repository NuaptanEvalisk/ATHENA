
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tools-menu.scm
;; DESCRIPTION : the tools menu
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus tools-menu)
  (:use (athena athena tm-tools)
        (athena athena tm-vault)
        (athena athena tm-vault-namespaces)
        (athena athena tm-websites)
        (athena tools shortcut-listing)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The Tools menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind tools-menu
  (-> "Macros"
      (link source-macros-menu))
  ("Command palette" (command-palette-show))
  ("Quick switcher" (open-quick-switcher))
  ("Vault Explorer" (open-vault-explorer))
  ("Namespace Explorer" (open-namespace-explorer))
  ("Namespace Manager" (open-namespace-manager))
  ("Export namespace" (namespace-export-show))
  ("Websites manager" (open-websites-manager))
  ("Custom styles manager" (custom-styles-manager-show))
  ("Vault backup viewer" (open-vault-backup-viewer))
  ("Vault Bugcheck" (vault-bugcheck))
  ("Vault maintenance" (vault-maintenance))
  (-> "Artifacts"
      ("Build for entire vault" (artifacts-build-entire-vault))
      ("Build for current document" (artifacts-build-current-document)))
  ("Google Tasks" (google-tasks-show))
  ("Shortcuts listing" (list-all-shortcuts))
  (-> "Speech"      ("Off" (reset-preference "speech"))
      ---
      ("English" (set-preference "speech" "english"))
      ("French" (set-preference "speech" "french")))
  ---
  (-> "Update"
      ("Inclusions" (inclusions-gc))
      ("Pictures" (picture-gc))
      ("Plugins" (reinit-plugin-cache))
      ("Styles" (style-clear-cache)))
  (if (url-exists-in-path? "pdflatex")
      (-> "LaTeX"
          (link tmtex-menu)))
  (-> "References"
      (link ref-menu))
  (if supports-email?
      (-> "Email"
          ("Open mailbox" (email-open-mailbox))
          ("Retrieve email" (begin (email-pop) (email-open-inbox)))
          ---
          ("Pop server settings" (interactive email-settings))))
  (-> "Project"
      (link project-manage-menu))
  (-> "Statistics"
      ("Count characters" (show-character-count))
      ("Count words" (show-word-count))
      ("Count lines" (show-line-count)))
  ---
  ("Clear undo history" (clear-undo-history))
  ("Save auxiliary data" (toggle-save-aux))
  ("Show key presses" (toggle-show-kbd))
  ("Remote control" (toggle-remote-control-mode))
  ("Clean cache" (clean-athena-cache)))
