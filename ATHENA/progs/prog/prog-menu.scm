
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : prog-menu.scm
;; DESCRIPTION : program menus and icons
;; COPYRIGHT   : (C) 2010  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (prog prog-menu)
  (:use (generic format-drd) (generic generic-edit)
	(generic insert-menu)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main Format menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind prog-format-menu
  ("Font" (open-font-selector))
  ("Paragraph" (open-paragraph-format))
  (when (in-main-flow?)
    ("Page" (open-page-format)))
  (when (inside? 'table)
    ("Cell" (open-cell-properties))
    ("Table" (open-table-properties)))
  ---
  (-> "Whitespace" (link space-menu))
  (-> "Indentation" (link indentation-menu))
  (-> "Break" (link break-menu))
  ---
  (-> "Color"
      (if (== (get-preference "experimental alpha") "on")
	  (-> "Opacity" (link opacity-menu))
	  ---)
      (link color-menu))
  (-> "Adjust" (link adjust-menu))
  (-> "Transform" (link linear-transform-menu))
  (-> "Specific" (link specific-menu))
  (-> "Special" (link format-special-menu))
  (-> "Font effects" (link text-font-effects-menu))
  (assuming (== (get-preference "bitmap effects") "on")
    (-> "Graphical effects" (link text-effects-menu))))
