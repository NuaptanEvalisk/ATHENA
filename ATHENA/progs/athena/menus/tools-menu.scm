
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
        (athena tools shortcut-listing)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Dynamic menus for formats, languages, and AI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind ai-translate-menu
  (for (lan supported-languages)
    ((eval (upcase-first lan))
     (ai-translate lan (get-preference "ai")))))

(tm-define (run-proof-pipeline-menu-action)
  (run-proof-pipeline))

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
  ("Custom styles manager" (custom-styles-manager-show))
  ("Vault backup viewer" (open-vault-backup-viewer))
  ("Vault Bugcheck" (vault-bugcheck))
  ("Vault maintenance" (vault-maintenance))
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
  ("Run proof pipeline" (run-proof-pipeline-menu-action))
  ---
  (when (and (!= (get-preference "ai") "off")
             (selection-active-any?))
    ("AI Correct" (ai-correct (get-preference "ai")))
    (-> "AI Translate"
        (link ai-translate-menu)))
  ---
  ("Create web site" (open-website-builder))
  ;;(-> "Web"
  ;;    ("Create web site" (tmweb-interactive-build))
  ;;    ("Update web site" (tmweb-interactive-update)))
  ---
  ("Clear undo history" (clear-undo-history))
  ("Save auxiliary data" (toggle-save-aux))
  ("Show key presses" (toggle-show-kbd))
  ("Remote control" (toggle-remote-control-mode))
  ("Clean cache" (clean-athena-cache)))
