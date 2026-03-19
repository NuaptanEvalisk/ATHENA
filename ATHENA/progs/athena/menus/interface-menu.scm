
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : interface-menu.scm
;; DESCRIPTION : the interface menu
;; COPYRIGHT   : (C) 2026 Nuaptan Felix Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus interface-menu)
  (:use (athena menus view-menu)))

(menu-bind interface-menu
  ("Header bars" (toggle-visible-header))
  (when (visible-header?)
        ("Main icon bar" (toggle-visible-icon-bar 0))
        ("Mode dependent icons" (toggle-visible-icon-bar 1))
        ("Focus dependent icons" (toggle-visible-icon-bar 2))
        ("User provided icons" (toggle-visible-icon-bar 3)))
  ("Status bar" (toggle-visible-footer))
  (if (with-developer-tool?)
      ("Left side tools" (toggle-visible-side-tools 1))
      ("Right side tools" (toggle-visible-side-tools 0))
      ("GUI through markup" (toggle-markup-gui)))
  ---
  ("Search toolbar" (toggle-bottom-bar "search"))
  ("Replace toolbar" (toggle-bottom-bar "replace"))
  ("Database toolbar" (toggle-bottom-bar "database"))
  ("Animation toolbar" (toggle-bottom-bar "animate"))
  ---
  ("Database tool" (toggle-preference "database tool"))
  ("Debugging tool" (toggle-preference "debugging tool"))
  ("Developer tool" (toggle-preference "developer tool"))
  ("Linking tool" (toggle-preference "linking tool"))
  ("Presentation tool" (toggle-preference "presentation tool"))
  ("Remote tool" (toggle-preference "remote tool"))
  ("Source macros tool" (toggle-preference "source tool"))
  ("Versioning tool" (toggle-preference "versioning tool")))
