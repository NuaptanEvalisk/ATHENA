;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tmtex.scm
;; DESCRIPTION : Scheme integration surface for native LaTeX export
;; COPYRIGHT   : (C) 2002  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert latex tmtex)
  (:use (convert tools tmpre)
        (convert rewrite tmtm-brackets)
        (doc tmdoc-markup)))

;; The exporter itself is native.  These callbacks expose only data or
;; functionality owned by other Scheme subsystems.

(tm-define (latex-native-key-rewrite kind body)
  (tm->stree
    (if (== kind "key*") (tmdoc-key* body) (tmdoc-key body))))

(tm-define (latex-native-cardlink-default-body destination)
  (if (and (defined? 'cardlink-default-link-body)
           (defined? 'stree->tree))
      (tree->stree (cardlink-default-link-body (stree->tree destination)))
      destination))

(tm-define (latex-native-transclude-fallback l)
  (if (defined? 'vault-resolve-transclude-content)
      (vault-resolve-transclude-content
        (stree->tree (car l))
        (stree->tree (cadr l))
        (stree->tree (caddr l))
        (stree->tree (cadddr l)))
      '(strong "Broken Transclusion: vault resolver unavailable.")))

(tm-define (texmacs->latex x opts)
  (latex-export-convert-top x opts current-save-source current-save-target))
