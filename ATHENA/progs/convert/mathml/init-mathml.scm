;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-mathml.scm
;; DESCRIPTION : Explicit native v3 Presentation MathML import
;; COPYRIGHT   : (C) 2026 ATHENA contributors
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert mathml init-mathml))

;; No recognizer or suffix takeover: HTML's v2 MathML route stays unchanged
;; until cutover. Paste-as selects this format explicitly and requests its
;; -snippet converter through generic->texmacs.
(define-format mathml-v3
  (:name "MathML (native v3)"))

(tm-define (mathml-v3->texmacs source)
  (native-mathml->tree-v3 (parse-xml source)))

;; Return the native tree directly. A texmacs-stree intermediate would erase
;; typed script policies, accent stretch, grid formatting and block numbering.
(converter mathml-v3-snippet texmacs-tree
  (:function mathml-v3->texmacs))

(converter mathml-v3-document texmacs-tree
  (:function mathml-v3->texmacs))

(converter mathml-v3-stree texmacs-tree
  (:function native-mathml->tree-v3))
