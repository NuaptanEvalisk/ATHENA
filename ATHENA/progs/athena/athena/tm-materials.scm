;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-materials.scm
;; DESCRIPTION : Vault-native Materials citations and referenced-material lists
;; COPYRIGHT   : (C) 2026  Nuaptan Felix Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-materials))

(define (materials-result-tuple? value minimum-arity)
  (and (tree? value)
       (tree-func? value 'tuple)
       (>= (tree-arity value) minimum-arity)))

(define (materials-document-citation-style)
  (if (init-has? "materials-csl-style")
      (get-init "materials-csl-style")
      (get-preference "materials csl style")))

(tm-define (materials-set-document-citation-style style)
  (init-env "materials-csl-style" style)
  (if (vault-active?)
      (materials-update-current-document)
      (set-message (string-append "Citation style: " style) "Materials")))

(tm-define (materials-use-default-citation-style)
  (init-default-one "materials-csl-style")
  (if (vault-active?)
      (materials-update-current-document)
      (set-message "Citation style follows Preferences" "Materials")))

(tm-define (insert-material-citation)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "Load a vault before inserting a Material citation"
                   "Materials")
      (material-choose-citation (materials-document-citation-style)
        (lambda (result)
          (when (materials-result-tuple? result 3)
            (let* ((items (tree->stree (tree-ref result 0)))
                   (text (tree->string (tree-ref result 1)))
                   (uri (tree->string (tree-ref result 2)))
                   (rendered (if (string-null? uri) text `(hlink ,text ,uri))))
              (insert `(material-citation ,items ,rendered))))))))

(kbd-commands
  ("?" "Insert Material Citation"
       (if (in-text?) (insert-material-citation))))

(tm-define (insert-referenced-materials)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "Load a vault before inserting referenced Materials"
                   "Materials")
      (material-choose-references (lambda (manual)
        (when (and (tree? manual) (tree-func? manual 'tuple))
          (insert `(referenced-materials
                    ""
                    ,(tree->stree manual)
                    (document "")))
          (materials-update-current-document))))))

(tm-define (materials-update-current-document)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault" "Materials")
      (with result (materials-update-document
                     (buffer-tree) (materials-document-citation-style))
        (if (and (materials-result-tuple? result 2)
                 (== (tree->string (tree-ref result 0)) "ok"))
            (begin
              (let ((root (buffer-tree)))
                (tree-assign! root (tree-ref result 1)))
              (set-message "Updated Material citations and referenced list"
                           "Materials"))
            (set-message
              (if (materials-result-tuple? result 2)
                  (tree->string (tree-ref result 1))
                  "Could not update Materials")
              "Materials error")))))

(define (materials-focused-reference-list)
  (tree-innermost 'referenced-materials #t))

(define (materials-reference-uuids node)
  (if (and node (tree-func? node 'referenced-materials 3)
           (tree-func? (tree-ref node 1) 'tuple))
      (map tree->string (tree-children (tree-ref node 1)))
      '()))

(tm-define (materials-append-references)
  (:interactive #t)
  (and-with node (materials-focused-reference-list)
    (material-choose-references (lambda (chosen)
      (when (and (tree->path node) (tree? chosen) (tree-func? chosen 'tuple))
        (let* ((old (materials-reference-uuids node))
               (added (map tree->string (tree-children chosen)))
               (all (list-remove-duplicates (append old added))))
          (tree-set! node 1 `(tuple ,@all))
          (materials-update-current-document)))))))

(tm-define (materials-set-reference-style style)
  (and-with node (materials-focused-reference-list)
    (tree-set! node 0 style)
    (materials-update-current-document)))

(tmfs-load-handler (material name)
  (tree->stree (material-info-page name)))

(tmfs-title-handler (material name doc)
  "Material")

(tmfs-permission-handler (material name kind)
  (== kind "read"))
