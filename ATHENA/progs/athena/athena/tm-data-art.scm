
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-data-art.scm
;; DESCRIPTION : DataArt cover generation for PDF export
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-data-art))

(define-public (data-art-enabled?)
  (get-boolean-preference "texmacs->pdf:data-art cover"))

(define-public (data-art-pdf-target? fname)
  (and (url? fname) (== (url-suffix fname) "pdf")))

(define (data-art-tool-dir)
  "$ATHENA_PATH/tools/data-art")

(define (data-art-script)
  (url-append (data-art-tool-dir) "athena_data_art.py"))

(define (data-art-venv-dir)
  (url-append (data-art-tool-dir) ".venv"))

(define (data-art-uv-cache-dir)
  "$ATHENA_HOME_PATH/system/cache/uv")

(define (data-art-temp-url suffix)
  (url-glue (url-temp) suffix))

(define (data-art-cover-tree cover)
  `(document
     (vspace* "1fn")
     (center (image ,(url->system cover) "0.92par" "" "" ""))
     (vspace "1.5fn")))

(define (data-art-cover-doc-misc cover)
  `(doc-misc
     (document
       (vspace* "1fn")
       (center (image ,(url->system cover) "0.72par" "" "" ""))
       (vspace "1.5fn"))))

(define (data-art-cover-document? t width)
  (and (pair? t)
       (eq? (car t) 'document)
       (== (length t) 4)
       (equal? (cadr t) '(vspace* "1fn"))
       (let ((image-line (caddr t)))
         (and (pair? image-line)
              (eq? (car image-line) 'center)
              (== (length image-line) 2)
              (let ((image (cadr image-line)))
                (and (pair? image)
                     (eq? (car image) 'image)
                     (>= (length image) 3)
                     (equal? (caddr image) width)))))
       (equal? (cadddr t) '(vspace "1.5fn"))))

(define (data-art-doc-misc-cover? t)
  (and (pair? t)
       (eq? (car t) 'doc-misc)
       (nnull? (cdr t))
       (data-art-cover-document? (cadr t) "0.72par")))

(define (data-art-body-cover? t)
  (data-art-cover-document? t "0.92par"))

(define (data-art-cover-present-stree? body)
  (and (pair? body)
       (eq? (car body) 'document)
       (or (exists? data-art-body-cover? (cdr body))
           (exists?
            (lambda (child)
              (and (pair? child)
                   (eq? (car child) 'doc-data)
                   (exists? data-art-doc-misc-cover? (cdr child))))
            (cdr body)))))

(define (data-art-title-block? child)
  (and (pair? child)
       (in? (car child) '(doc-data title doc-title tmdoc-title tmdoc-title*
                          tmweb-title doc-title-block))))

(define (data-art-insert-cover-stree body cover)
  (if (and (pair? body) (eq? (car body) 'document))
      (let* ((children (cdr body))
             (cover-tree (data-art-cover-tree cover)))
        (cond ((and (pair? children) (data-art-title-block? (car children)))
               `(document ,(car children) ,cover-tree ,@(cdr children)))
              (else
               `(document ,cover-tree ,@children))))
      body))

(define (data-art-insert-cover-in-doc-data-child child cover)
  (if (and (pair? child) (eq? (car child) 'doc-data))
      `(doc-data ,@(cdr child) ,(data-art-cover-doc-misc cover))
      child))

(define (data-art-insert-cover-in-doc-data-stree body cover)
  (if (data-art-cover-present-stree? body)
      body
      (if (and (pair? body) (eq? (car body) 'document))
          (let* ((children (cdr body))
                 (hit? #f)
                 (new-children
                  (map (lambda (child)
                         (if (and (not hit?)
                                  (pair? child)
                                  (eq? (car child) 'doc-data))
                             (begin
                               (set! hit? #t)
                               (data-art-insert-cover-in-doc-data-child child cover))
                             child))
                       children)))
            (if hit?
                `(document ,@new-children)
                (data-art-insert-cover-stree body cover)))
          body)))

(define (data-art-seed-string buf)
  (string-append
   "ATHENA DataArt cover seed\n"
   (url->system buf)
   "\n\n"
   (object->string (tree->stree (buffer-get-body buf)))))

(define-public (data-art-generate-cover buf)
  (let* ((seed (data-art-temp-url ".txt"))
         (cover (data-art-temp-url ".png"))
         (tool-dir (data-art-tool-dir))
         (venv-dir (data-art-venv-dir))
         (uv-cache-dir (data-art-uv-cache-dir))
         (script (data-art-script))
         (cmd (string-append
               "UV_CACHE_DIR="
               (escape-shell (url->system uv-cache-dir))
               " UV_PROJECT_ENVIRONMENT="
               (escape-shell (url->system venv-dir))
               " uv run --quiet --locked --project "
               (escape-shell (url->system tool-dir))
               " python "
               (escape-shell (url->system script))
               " --input "
               (escape-shell (url->system seed))
               " --output "
               (escape-shell (url->system cover)))))
    (when (not (url-exists? uv-cache-dir))
      (system-mkdir uv-cache-dir))
    (string-save (data-art-seed-string buf) seed)
    (system cmd)
    (system-remove seed)
    (if (url-exists? cover)
        cover
        (begin
          (display* "ATHENA] data-art warning: cover generation failed\n")
          #f))))

(define-public (data-art-insert-cover-in-buffer buf cover)
  (let* ((body (buffer-get-body buf))
         (new-body (stree->tree
                    (let ((stree-body (tree->stree body)))
                      (if (data-art-cover-present-stree? stree-body)
                          stree-body
                          (data-art-insert-cover-stree stree-body cover))))))
    (buffer-set-body buf new-body)))

(define-public (data-art-insert-cover-in-doc-data-buffer buf cover)
  (let* ((body (buffer-get-body buf))
         (new-body
          (stree->tree
           (data-art-insert-cover-in-doc-data-stree (tree->stree body) cover))))
    (buffer-set-body buf new-body)))

(define-public (data-art-cover-present-in-buffer? buf)
  (data-art-cover-present-stree? (tree->stree (buffer-get-body buf))))
