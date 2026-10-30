
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : graphics-properties.scm
;; DESCRIPTION : properties of the graphical tags
;; COPYRIGHT   : (C) 2011  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module
 (graphics graphics-properties)
 (:use (utils library cursor) (utils library tree) (utils edit variants)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Properties of the graphical tags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define
 (graphical-context? t)
 (tm-in? t (node-group-members "graphical-tag")))

(tm-define
 (graphical-text-context? t)
 (tm-in? t (node-group-members "graphical-text-tag")))

(tm-define
 (graphical-long-text-context? t)
 (tm-in? t (node-group-members "graphical-long-text-tag")))

(tm-define (graphical-text-at-context? t)
  (and (graphical-text-context? t) (>= (tm-arity t) 2)))

(tm-define (graphical-long-text-at-context? t)
  (and (graphical-long-text-context? t) (>= (tm-arity t) 2)))

(tm-define (graphical-text-arg-context? t)
  (and (graphical-text-context? t) (< (tm-arity t) 2)))

(tm-define (inside-graphical-text?)
  (and-with t (tree-innermost graphical-text-context?)
    (and-with u (tree-ref t :down)
      (and-with i (tree-index u)
        (tree-accessible-child? t i)))))

(tm-define gr-tags-user      (list))
(tm-define gr-tags-all (node-group-members "graphical-tag"))
(tm-define gr-tags-curves (node-group-members "graphical-curve-tag"))
(tm-define
 gr-tags-noncurves
 (append
  (node-group-members "graphical-atomic-tag")
  (node-group-members "graphical-text-tag")
  (node-group-members "graphical-group-tag")))

(tm-define (graphical-user-tag? l)
  (in? l gr-tags-user))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; List of graphical attributes and their properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (gr-prefixed? attr)
  (string-starts? attr "gr-"))

(tm-define (gr-prefix attr)
  (string-append "gr-" attr))

(tm-define (gr-unprefix attr)
  (substring attr 3 (string-length attr)))

(define-table attribute-default-table
  ("gid" . "default")
  ("proviso" . "true")
  ("magnify" . "1")
  ("color" . "black")
  ("opacity" . "100%")
  ("point-style" . "disk")
  ("point-size" . "2.5ln")
  ("point-border" . "1ln")
  ("line-width" . "1ln")
  ("line-join" . "normal")
  ("line-caps" . "normal")
  ("line-effects" . "normal")
  ("line-portion" . "1")
  ("dash-style" . "none")
  ("dash-style-unit" . "5ln")
  ("arrow-begin" . "none")
  ("arrow-end" . "none")
  ("arrow-length" . "5ln")
  ("arrow-height" . "5ln")
  ("fill-color" . "none")
  ("fill-style" . "plain")
  ("text-at-halign" . "left")
  ("text-at-valign" . "base")
  ("text-at-repulse" . "off")
  ("text-at-snapping" . "1spc")
  ("doc-at-valign" . "top")
  ("doc-at-width" . "1par")
  ("doc-at-hmode" . "min")
  ("doc-at-ppsep" . "0fn")
  ("doc-at-border" . "0ln")
  ("doc-at-padding" . "0spc")
  ("pen-enhance" . "gaussian"))

(tm-define (graphics-attribute-default attr)
  (if (gr-prefixed? attr)
      (graphics-attribute-default (gr-unprefix attr))
      (ahash-ref attribute-default-table attr)))

(tm-define (decode-dash x)
  (cond ((== x "default") "---")
        ((== x "10") ". . . . .")
        ((== x "11100") "- - - - -")
        ((== x "1111010") "- . - . -")
        ((string-alpha? x) x)
        (else "other")))

(tm-define (decode-arrow x)
  (cond ((== x "default") "")
        ((== x "none") "")
        ((== x "") "")
        ((== x "<gtr>") ">")
        ((== x "|<gtr>") "|>")
        ((== x "<gtr><gtr>") ">>")
        ((== x "<less>") "<")
        ((== x "<less>|") "<|")
        ((== x "<less><less>") "<<")
        ((string? x) x)
        (else "other")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Attributes of the graphical tags
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-common-attributes)
  '("gid" "proviso" "magnify" "color" "opacity"))

(tm-define (graphics-all-attributes)
  (map car (ahash-table->list attribute-default-table)))

(tm-define (graphics-attributes tag)
  (graphics-common-attributes))

(tm-define (graphics-attributes tag)
  (:require (== tag 'point))
  (append (graphics-common-attributes)
          '("fill-color" "point-style" "point-size" "point-border")))

(tm-define
 (graphics-attributes tag)
 (:require
  (or (node-group-contains? "graphical-curve-tag" tag)
      (graphical-user-tag? tag)))
 (append
  (graphics-common-attributes)
  '("fill-color"
    "line-width"
    "line-join"
    "line-caps"
    "line-effects"
    "line-portion"
    "dash-style"
    "dash-style-unit"
    "arrow-begin"
    "arrow-end"
    "arrow-length"
    "arrow-height")))

(tm-define
 (graphics-attributes tag)
 (:require (node-group-contains? "graphical-text-tag" tag))
 (append
  (graphics-common-attributes)
  '("text-at-halign" "text-at-valign" "text-at-repulse" "text-at-snapping")))

(tm-define
 (graphics-attributes tag)
 (:require (node-group-contains? "graphical-long-text-tag" tag))
 (append
  (graphics-common-attributes)
  '("text-at-halign"
    "doc-at-valign"
    "text-at-repulse"
    "text-at-snapping"
    "fill-color"
    "doc-at-width"
    "doc-at-hmode"
    "doc-at-ppsep"
    "doc-at-border"
    "doc-at-padding")))

(tm-define
 (graphics-attributes tag)
 (:require (node-group-contains? "graphical-penscript-tag" tag))
 (append
  (graphics-common-attributes)
  '("pen-enhance"
    "line-width"
    "line-join"
    "line-caps"
    "line-effects"
    "line-portion"
    "dash-style"
    "dash-style-unit"
    "arrow-begin"
    "arrow-end"
    "arrow-length"
    "arrow-height")))

(tm-define
 (graphics-attributes tag)
 (:require (node-group-contains? "graphical-group-tag" tag))
 (graphics-all-attributes))

(tm-define (graphics-attribute? tag attr)
  (in? attr (graphics-attributes tag)))

(tm-define
 (graphics-valign-var t)
 (cond
   ((in? t (node-group-members "graphical-short-text-tag")) "text-at-valign")
   ((in? t (node-group-members "graphical-long-text-tag")) "doc-at-valign")
   ((tree? t) (graphics-valign-var (tree-label t)))
   ((pair? t) (graphics-valign-var (car t)))
   (else "text-at-valign")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Attributes for editing modes
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-mode-attributes mode)
  (cond ((func? mode 'edit 1) (graphics-attributes (cadr mode)))
        ((func? mode 'hand-edit 1) (graphics-attributes (cadr mode)))
        ((== mode '(group-edit props)) (graphics-all-attributes))
        ((== mode '(group-edit edit-props)) (graphics-all-attributes))
        (else '())))

(tm-define (graphics-mode-attribute? mode attr)
  (if (== mode '(group-edit edit-props))
      (and (native-graphics-edit-props-active?)
           (native-graphics-selection-supports-property? attr))
      (in? attr (graphics-mode-attributes mode))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Refined properties concerning arity and types of children
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-minimal? obj)
  (or (tm-in? obj '(point text-at math-at document-at))
      (== (tm-arity obj) (tag-minimal-arity (tm-car obj)))))

(tm-define (graphics-incomplete? obj)
  (< (tm-arity obj) (tag-minimal-arity (tm-car obj))))

(tm-define (graphics-complete? obj)
  (with maximum (tag-maximal-arity (tm-car obj))
    (and (>= maximum 0) (>= (tm-arity obj) maximum))))

(tm-define (graphics-complete obj)
  (list obj #f))
