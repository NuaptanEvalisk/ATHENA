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
(check (equal? (embedded-suffix embedded) "png")
       "native embedded suffix returns filename extension")
(define extensionless
  (stree->tree '(image (tuple (raw-data "abc") "png") "1cm" "" "" "")))
(check (equal? (embedded-suffix extensionless) "png")
       "native embedded suffix falls back to extensionless filename")
(check (not (embedded-suffix linked))
       "native embedded suffix rejects linked image")

(define proposal-source
  (stree->tree '(image (tuple (raw-data "abc") "named.png") "1cm" "" "" "")))
(check (equal? (embedded-propose proposal-source 3)
               (url->string (url-relative (current-buffer) "named.png")))
       "native embedded proposal preserves named file with suffix")

(define save-target (string->url "/tmp/athena-embedded-save-test.bin"))
(save-embedded-image embedded save-target)
(check (equal? (string-load save-target) "abc")
       "native save-embedded-image writes raw payload")

(buffer-set-body
  (current-buffer)
  (stree->tree '(document
                  (image (tuple (raw-data "linked-payload") "linked.bin")
                         "1cm" "" "" ""))))
(update-current-buffer)
(define link-target (string->url "/tmp/athena-embedded-link-test.bin"))
(define expected-link (url->string (url-delta (current-buffer) link-target)))
(link-embedded-image (tree-ref (buffer-tree) 0) link-target)
(check (equal? (string-load link-target) "linked-payload")
       "native link-embedded-image saves raw payload")
(check (equal? (tree->string (tree-ref (buffer-tree) 0 0)) expected-link)
       "native link-embedded-image replaces embedded tuple with relative link")
(check (linked-image-context? (tree-ref (buffer-tree) 0))
       "native linked image mutation updates context classification")

(buffer-set-body
  (current-buffer)
  (stree->tree
    '(document
       (concat
         (image (tuple (raw-data "same") "copy.bin") "1cm" "" "" "")
         (image (tuple (raw-data "same") "copy.bin") "2cm" "" "" "")
         (image (tuple (raw-data "other") "other.bin") "3cm" "" "" "")))))
(update-current-buffer)
(define copies-target (string->url "/tmp/athena-embedded-copies-test.bin"))
(define expected-copies-link
  (url->string (url-delta (current-buffer) copies-target)))
(link-embedded-image-copies (tree-ref (buffer-tree) 0 0) copies-target)
(check (equal? (string-load copies-target) "same")
       "native link copies saves selected embedded payload")
(check (equal? (tree->string (tree-ref (buffer-tree) 0 0 0))
               expected-copies-link)
       "native link copies replaces selected embedded source")
(check (equal? (tree->string (tree-ref (buffer-tree) 0 1 0))
               expected-copies-link)
       "native link copies replaces structurally equal embedded source")
(check (embedded-image-context? (tree-ref (buffer-tree) 0 2))
       "native link copies preserves different embedded source")

;; The chooser callbacks re-find the embedded image from the cursor.
(buffer-set-body
  (current-buffer)
  (stree->tree '(document
                  (image (tuple (raw-data "callback") "callback.bin")
                         "1cm" "" "" ""))))
(update-current-buffer)
(tree-go-to (tree-ref (buffer-tree) 0) 0 0 0 :end)
(define callback-target (string->url "/tmp/athena-embedded-callback-test.bin"))
(embedded-saver callback-target)
(check (equal? (string-load callback-target) "callback")
       "native embedded-saver callback finds image at cursor")
(embedded-linker callback-target)
(check (linked-image-context? (tree-ref (buffer-tree) 0))
       "native embedded-linker callback links image at cursor")

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
