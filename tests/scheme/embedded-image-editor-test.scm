;; Native linked/embedded image classification and actor-owned embedding.
(import-from (generic embedded-edit))
(init-style "generic")

(define (check condition message . details)
  (unless condition
    (apply error (cons message details))))

(define linked
  (stree->tree '(image "linked.png" "1cm" "" "" "")))
(define embedded
  (stree->tree '(image (tuple (raw-data "abc") "embedded.png")
                       "1cm" "" "" "")))

(check (image-context? linked)
       "native image-context recognizes image arity five")
(check (linked-image-context? linked)
       "native linked-image predicate recognizes path-backed image")
(check (not (embedded-image-context? linked))
       "linked image is not embedded")
(check (embedded-image-context? embedded)
       "native embedded-image predicate recognizes raw-data tuple")
(check (not (linked-image-context? embedded))
       "embedded image is not linked")

(define fixture "$ATHENA_PATH/misc/images/windows/SmallTile.png")
(buffer-set-body
  (current-buffer)
  (stree->tree `(document (image ,fixture "1cm" "" "" ""))))
(update-current-buffer)
(define active-image (tree-ref (buffer-tree) 0))
(embed-image active-image)
(check (embedded-image-context? (tree-ref (buffer-tree) 0))
       "native embed-image converts active linked image to raw-data")
(check (equal? (tree->stree (tree-ref (buffer-tree) 0 0 1)) "SmallTile.png")
       "native embed-image stores linked filename tail")
(check (> (string-length (tree->string (tree-ref (buffer-tree) 0 0 0 0))) 0)
       "native embed-image stores nonempty image bytes")

;; Cursor-local embedding reproduces tree-innermost ... #t semantics.
(buffer-set-body
  (current-buffer)
  (stree->tree `(document "before" (image ,fixture "2cm" "" "" "") "after")))
(update-current-buffer)
(tree-go-to (tree-ref (buffer-tree) 1) 0 :end)
(embed-this-image)
(check (embedded-image-context? (tree-ref (buffer-tree) 1))
       "native embed-this-image finds linked image at cursor")

;; Recursive embedding converts linked images while leaving an already
;; embedded image unchanged.
(buffer-set-body
  (current-buffer)
  (stree->tree
    `(document
       (concat
         (image ,fixture "1cm" "" "" "")
         (image (tuple (raw-data "keep") "already.png") "1cm" "" "" "")))))
(update-current-buffer)
(embed-all-images)
(check (embedded-image-context? (tree-ref (buffer-tree) 0 0))
       "native embed-all-images recursively embeds linked image")
(check (equal? (tree->string (tree-ref (buffer-tree) 0 1 0 0 0)) "keep")
       "native embed-all-images preserves existing embedded bytes")

(init-env "page-medium" "paper")
(update-current-buffer)
(update-forced)
(print-to-file (string->url (string-append (getenv "HOME") "/evaluation.pdf")))
#t
