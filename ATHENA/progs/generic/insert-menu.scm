
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : insert-menu.scm
;; DESCRIPTION : menus for inserting new structure
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic insert-menu)
  (:use (utils edit selections)
	(athena athena tm-materials)
	(athena athena tm-reverse-hierarchy-graph)
	(generic generic-edit)
	(generic format-drd)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Insert links
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind insert-link-menu
  ("Wikilink" (insert-wikilink))
  ("Transclusion" (insert-transclude))
  ---
  (when (not (selection-active-non-small?))
    ("Label" (make-label))
    ("Reference" (make 'reference))
    ("Page reference" (make 'pageref)))
  ---
  (when (not (selection-active?))
    ("Include" (choose-file make-include "Include file" "")))
  (when (not (selection-active-non-small?))
    ("Link to URL" (make 'slink))
    ("Hyperlink" (make 'hlink))
    ("Card Link" (make 'cardlink))
    ("Action" (make 'action)))
  (if (and (style-has? "std-dtd") (in-text?))
      ---
      (when (not (selection-active-non-small?))
        (-> "Index entry"
            ("Main" (make 'index))
            ("Sub" (make 'subindex))
            ("Subsub" (make 'subsubindex))
            ("Complex" (make 'index-complex))
            ---
            ("Interjection" (make 'index-line)))
        (-> "Glossary entry"
            ("Regular" (make 'glossary))
            ("Explained" (make 'glossary-explain))
            ("Duplicate" (make 'glossary-dup))
            ---
            ("Interjection" (make 'glossary-line))))
      (-> "Alternate"
          ("Table of contents"
           (make-alternate "Name of table of contents" "toc" 'with-toc))
          ("Index"
           (make-alternate "Name of index" "idx" 'with-index))
          ("Glossary"
           (make-alternate "Name of glossary" "gly" 'with-glossary))
          ("List of figures"
           (make-alternate "Name of list of figures" "figure" 'with-figure-list))
          ("List of tables"
           (make-alternate "Name of list of tables" "table" 'with-table-list)))
      ---
      ("Reference to note" (make-note-ref))
      (-> "Text for note"
          ("Inline" (make-note-inline))
          ("Wide" (make-note-wide))
          (when (in-main-flow?)
            ("Footnote" (make-note-footnote)))))
  )

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Insert images
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind insert-image-menu
  (if (and (style-has? "env-float-dtd") (in-text?))
      (when (not (selection-active-non-small?))
        ("Small figure"
         (wrap-selection-small
           (make 'small-figure)))
        ("Big figure"
         (wrap-selection-small
           (insert-go-to '(big-figure "" (document "")) '(0 0))))
        ---))
  ("Link image" (choose-file make-link-image "Load image" "image"))
  ("Insert image" (choose-file make-inline-image "Load image" "image"))
  ("Thumbnails" (interactive make-thumbnails))

  ---
  ("Draw image" (make-graphics))
  )

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main Insert menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind texmacs-insert-menu
  ("Handwritten Symbol" (handwriting-symbol-pane-show))
  ("Material citation" (insert-material-citation))
  ("Referenced Materials" (insert-referenced-materials))
  ---
  (-> "Macro" (link insert-macro-menu))
  (if (not (in-text?))
      ("Text" (make 'text)))
  (if (not (in-math?))
      (-> "Mathematics" (link insert-math-menu)))
  (-> "Table" (link insert-table-menu))
  (-> "Image" (link insert-image-menu))
  (-> "Graph"
      ("Reverse Hierarchy" (insert-reverse-hierarchy-graph)))
  (-> "Link" (link insert-link-menu))
  ("Build warning" (make-experimental-build-warning))
  (if (style-has? "std-fold-dtd")
      (-> "Fold" (link insert-fold-menu))))

(menu-bind insert-menu
  (if (in-text?) (link text-menu))
  (if (in-math?) (link math-menu))
  (if (not (or (in-text?) (in-math?))) (link texmacs-insert-menu)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main Insert icons
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind texmacs-insert-icons
  /
  (=> (balloon (icon "tm_macro") "Insert a personal macro")
      (link insert-macro-menu))
  (if (not (in-text?))
      ((balloon (icon "tm_textual") "Insert plain text")
       (make 'text)))
  (if (not (in-math?))
      (=> (balloon (icon "tm_math") "Insert mathematics")
	  (link insert-math-menu)))
  (=> (balloon (icon "tm_table") "Insert a table")
      (link insert-table-menu))
  (=> (balloon (icon "tm_image") "Insert a picture")
      (link insert-image-menu))
  (=> (balloon (icon "tm_link") "Insert a link")
      (link insert-link-menu))
  (if (style-has? "std-fold-dtd")
      (=> (balloon (icon "tm_switch") "Switching and folding")
          (link insert-fold-menu))))
