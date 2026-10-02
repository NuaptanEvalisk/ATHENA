
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : main-menu.scm
;; DESCRIPTION : the default main menu of TeXmacs
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus main-menu)
  (:use (utils library cursor)
        (athena athena tm-vault)))

(tm-define (style-menu) (get-style-menu))
(tm-define (add-package-menu) (get-add-package-menu))
(tm-define (remove-package-menu) (get-remove-package-menu))
(tm-define (toggle-package-menu) (get-toggle-package-menu))
(menu-bind bookmarks-menu)
(menu-bind test-menu)

(tm-menu (athena-focus-menu)
  (link focus-menu)
  ---
  ("Node properties..." (node-properties-show (focus-tree)))
  (if (tree-innermost 'transclude #t)
    ---
    (link vault-transclusion-focus-menu))
  (if (tree-innermost 'referenced-materials #t)
    ---
    (link materials-focus-menu)))

(tm-menu (window-list-menu)
  (for (win (window-list))
    (let* ((buf (window-to-buffer win))
           (title (buffer-get-title buf))
           (title* (if (== title "") (url->system (url-tail buf)) title))
           (mod? (buffer-menu-modified? buf))
           (short-name `(verbatim ,(string-append title* (if mod? " *" ""))))
           (active? (== (current-window) win)))
      ((check (eval short-name) "v" active?)
       (switch-to-window win)))))

(menu-bind workspace-menu
  ("New tab" (new-document))
  ("New floating window" (open-document-window #t))
  ("Configure Font for Vault" (configure-font-for-vault))
  ("Run global transformation" (run-global-transformation))
  ("AUDMAP REPL" (audmap-repl-show))
  ---
  (link athena-workspace-utilities-menu))

(tm-menu (document-menu)
  (former)
  ---
  ("Flatten transclusions into new document" (vault-flatten-document)))
