
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-markdown.scm
;; DESCRIPTION : AOFM-backed Markdown snippet conversion
;; COPYRIGHT   : (C) 2026  Nuaptan Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert markdown init-markdown))

(define-format markdown
  (:name "Markdown")
  (:suffix "md"))

(define-format chatgpt
  (:name "ChatGPT"))

(converter markdown-snippet texmacs-tree
  (:function aofm-markdown->texmacs))

(converter chatgpt-snippet texmacs-tree
  (:function aofm-chatgpt->texmacs))
