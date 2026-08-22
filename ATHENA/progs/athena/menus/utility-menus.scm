
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : utility-menus.scm
;; DESCRIPTION : utility commands grouped by their owning main menus
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus utility-menus)
  (:use (athena athena tm-tools)
        (athena athena tm-vault)
        (athena athena tm-vault-namespaces)
        (athena athena tm-websites)
        (athena tools shortcut-listing)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Navigation and panes
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind athena-go-utilities-menu
  ("Command palette" (command-palette-show))
  ("Quick switcher" (open-quick-switcher)))

(menu-bind athena-view-panes-menu
  ("Vault Explorer" (open-vault-explorer))
  ("Namespace Explorer" (open-namespace-explorer))
  ("Vault backup viewer" (open-vault-backup-viewer)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Workspace management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind athena-workspace-utilities-menu
  ("Namespace Manager" (open-namespace-manager))
  ("Websites manager" (open-websites-manager))
  ("Materials manager" (materials-manager-show))
  ("Custom styles manager" (custom-styles-manager-show))
  (-> "Vault"
      ("Bugcheck" (vault-bugcheck))
      ("Maintenance" (vault-maintenance)))
  (-> "Artifacts"
      ("Build for entire vault" (artifacts-build-entire-vault))
      ("Build for current document" (artifacts-build-current-document)))
  ("Google Tasks" (google-tasks-show))
  (-> "Refresh caches"
      ("Plugins" (reinit-plugin-cache))
      ("Styles" (style-clear-cache)))
  ("Clean cache" (clean-athena-cache))
  (if supports-email?
      (-> "Email"
          ("Open mailbox" (email-open-mailbox))
          ("Retrieve email" (begin (email-pop) (email-open-inbox)))
          ---
          ("Pop server settings" (interactive email-settings)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; File and document operations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind athena-file-utilities-menu
  ("Export namespace" (namespace-export-show))
  (if (url-exists-in-path? "pdflatex")
      (-> "LaTeX" (link tmtex-menu))))

(menu-bind athena-document-utilities-menu
  (-> "Macros" (link source-macros-menu))
  (-> "Refresh auxiliary data"
      ("Inclusions" (inclusions-gc))
      ("Pictures" (picture-gc)))
  (-> "References"
      (link ref-menu))
  (-> "Project"
      (link project-manage-menu))
  (-> "Statistics"
      ("Count characters" (show-character-count))
      ("Count words" (show-word-count))
      ("Count lines" (show-line-count)))
  ("Save auxiliary data" (toggle-save-aux)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Editing, interface, and help
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind athena-edit-utilities-menu
  ("Clear undo history" (clear-undo-history)))

(menu-bind athena-interface-utilities-menu
  (-> "Speech"
      ("Off" (reset-preference "speech"))
      ---
      ("English" (set-preference "speech" "english"))
      ("French" (set-preference "speech" "french")))
  ("Show key presses" (toggle-show-kbd))
  ("Remote control" (toggle-remote-control-mode)))

(menu-bind athena-help-utilities-menu
  ("Shortcuts listing" (list-all-shortcuts)))
