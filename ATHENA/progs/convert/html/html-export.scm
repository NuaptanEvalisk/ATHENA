;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Native HTML exporter: owned resource callbacks and converter entrypoints.
;; Copyright (C) 2002 Joris van der Hoeven, David Allouche
;; Copyright (C) 2026 ATHENA authors
;; GNU GPL version 3 or later; see LICENSE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert html html-export)
  (:use (convert latex init-latex)
        (convert latex tmtex)
        (convert html htmlout)))

(define (tmhtml-prepare-source x)
  (let ((source (if (tree? x) (latex-export-prepare-v3-source x) x)))
    (native-html-prepare-source source)))

(define (tmhtml-preference-color key fallback)
  (with color (get-preference key)
    (if (or (== color "") (== color "default")) fallback color)))

(define (tmhtml-css-header)
  (native-html-stylesheet
    (tmcolor->htmlcolor (tmhtml-preference-color "locus-color" "#404080"))
    (tmcolor->htmlcolor (tmhtml-preference-color "visited-color" "#702070"))
    (tmcolor->htmlcolor (tmhtml-preference-color "gui selection color" "red"))
    (tmcolor->htmlcolor
      (tmhtml-preference-color "white document background override color" "#f7f3e8"))
    (== (get-preference "override white document background") "on")))

(define (tmcolor->htmlcolor x) (native-html-color x #f))

(define (tmhtml-color-valid? color)
  (and color (!= color "") (!= color "none")))

(define (tmhtml-inline-math? source)
  (and (pair? source)
       (or (== (car source) 'math)
           (list-any tmhtml-inline-math? (cdr source)))))

(define (tmhtml-enunciation-color kind group fallback)
  (let* ((specific (get-preference (string-append "vault " kind " color")))
         (group-key (string-append "vault " group " color"))
         (group-color (get-preference group-key)))
    (cond ((tmhtml-color-valid? specific) specific)
          ((tmhtml-color-valid? group-color) group-color)
          (else fallback))))

(define (tmhtml-with-file-init-one body var val)
  (if (and val (!= val ""))
      `(with ,var ,val ,body)
      body))

(define (tmhtml-with-file-initials doc body)
  (let* ((font (tmfile-init doc "font" #t))
         (math-font (tmfile-init doc "math-font" #t))
         (font-family (tmfile-init doc "font-family" #t)))
    (tmhtml-with-file-init-one
      (tmhtml-with-file-init-one
        (tmhtml-with-file-init-one body "font" font)
        "math-font" math-font)
      "font-family" font-family)))

(define (make-html-resource-adapter)
  (let ((export-target current-save-target)
        (tmhtml-image-serial 0)
        (tmhtml-image-cache (make-ahash-table))
        (tmhtml-image-root-url (unix->url "image"))
        (tmhtml-image-root-string "image")
        (tmhtml-web-formats '("gif" "jpg" "jpeg" "png" "bmp" "svg")))

    (define (initialize-resources)
      (set! tmhtml-image-cache (make-ahash-table))
      (let* ((suffix (url-suffix current-save-target))
    	 (n (+ (string-length suffix) 1)))
        (if (in? suffix '("html" "htm"))
    	(begin
    	  (set! tmhtml-image-serial 0)
    	  (set! tmhtml-image-root-url (url-unglue current-save-target n))
    	  (set! tmhtml-image-root-string
    		(url->unix (url-tail tmhtml-image-root-url))))
    	(begin
    	  (set! tmhtml-image-serial 0)
    	  (set! tmhtml-image-root-url (unix->url "image"))
    	  (set! tmhtml-image-root-string "image")))))

    (define (tmhtml-image-names ext)
      (set! tmhtml-image-serial (+ tmhtml-image-serial 1))
      (let* ((postfix (string-append
    		   "-" (number->string tmhtml-image-serial) "." ext))
    	 (name-url (url-glue tmhtml-image-root-url postfix))
    	 (name-string (string-append tmhtml-image-root-string postfix)))
        (values name-url name-string)))

    (define (magic-png-number)
      (let* ((css (get-preference "texmacs->html:css-stylesheet"))
             (s (url->string (url-tail css))))
        (if (string-starts? s "web-") 19000.0 20625.0)))

    (define (tmhtml-png source)
      (when (or (tmhtml-inline-math? source) (latex-native-v3-math? source))
        (error "Unsupported XML v3 HTML export: raster fallback would discard native mathematics"))
      (or (ahash-ref tmhtml-image-cache source)
          (receive (name-url name-string) (tmhtml-image-names "png")
            (with extents (print-snippet name-url source #t)
              (unless (url-exists? name-url)
                (error "HTML export: rendered picture was not produced" name-url))
              (with output (native-html-picture-output
                             source name-string extents (magic-png-number))
                (ahash-set! tmhtml-image-cache source output)
                output)))))

    (define (tmhtml-image-suffix name)
      (cond ((string? name) (url-suffix name))
            ((and (func? name 'tuple 2) (string? (caddr name)))
             (with suffix (url-suffix (caddr name))
               (if (== suffix "") (caddr name) suffix)))
            (else "")))

    (define (tmhtml-web-image-name name)
      (let* ((un (unix->url name))
             (source (url-relative current-save-source un))
             (target (url-relative current-save-target un)))
        (cond ((url-rooted-web? un) name)
              ((not (url-exists? source))
               (error "HTML export: image source does not exist" source))
              ((== source target) name)
              (else
                (receive (name-url name-string) (tmhtml-image-names (url-suffix un))
                  (system-copy source name-url)
                  name-string)))))

    (define (tmhtml-image l)
      ;; FIXME: Should also test that width and height are not magnifications.
      ;; Magnification-valued image dimensions still need rendered extents.
      (cond ((nin? (tmhtml-image-suffix (car l)) tmhtml-web-formats)
             ;; linked or embedded non-web image: convert & save to png  
             (tmhtml-png (cons 'image l)))
            ((and (func? (car l) 'tuple 2)
                  (func? (cadar l) 'raw-data 1)
                  ((@ (rnrs bytevectors) bytevector?) (cadr (cadar l)))
                  (string? (caddar l)))
             ;; embedded web image, extract it    
             (receive (name-url name-string)
                       (tmhtml-image-names (tmhtml-image-suffix (car l)))
               (bytes-save (cadr (cadar l)) name-url)
               (native-html-image-output
                 (cons 'image (cons name-string (cdr l))) name-string)))
            ((nstring? (first l))
             ;; treat complex images as generic markup
             (tmhtml-png (cons 'image l)))
            (else
              ;; web images
              (native-html-image-output (cons 'image l)
                                        (tmhtml-web-image-name (first l))))))

    (define (tmhtml-resource operation source source-url)
      (catch #t
        (lambda ()
          (let ((previous-source current-save-source)
                (previous-target current-save-target))
            (dynamic-wind
              (lambda ()
                (set! current-save-source (string->url source-url))
                (set! current-save-target export-target))
              (lambda ()
                (list #t
                  (cond
                    ((== operation "math")
                     (latex-export-convert-top
                       (car source) '(("texmacs->latex:mathjax" . "on"))
                       current-save-source current-save-target))
                    ((== operation "picture") (tmhtml-png source))
                    ((== operation "image") (tmhtml-image (cdr source)))
                    ((== operation "include")
                     (with u (url-relative current-save-source (unix->url (cadr source)))
                       (unless (url-exists? u)
                         (error "HTML export: included source does not exist" u))
                       (list (url->string u)
                             (tmhtml-prepare-source (tree-inclusion u)))))
                    ((== operation "document-plan")
                     (native-html-document-plan
                       (cadr source) (cdr (caddr source)) (tmhtml-css-header)
                       (get-preference "texmacs->html:css-stylesheet")
                       (string-append "ATHENA " (texmacs-version)) #t))
                    ((== operation "enunciation-color")
                     (tmhtml-enunciation-color (car source) (cadr source) (caddr source)))
                    (else (error "Unknown HTML resource operation" operation)))))
              (lambda ()
                (set! current-save-source previous-source)
                (set! current-save-target previous-target)))))
        (lambda args (list #f (object->string args)))))

    (initialize-resources)

    tmhtml-resource))

(tm-define (tmhtml-root source)
  (native-html-convert (tmhtml-prepare-source source)
                       (make-html-resource-adapter)
                       (url->string current-save-source)))

(tm-define (texmacs->html x opts)
  ;; Check before math/image dispatch and before stripping document wrappers.
  ;; A stree caller may already have prepared the metadata-bearing source.
  (set! x (tmhtml-prepare-source x))
  (if (tmfile? x)
      (let* ((body (tmfile-extract x 'body))
	     (style* (tmfile-extract x 'style))
	     (style (if (list? style*) style* (list style*)))
	     (lan (tmfile-language x))
             (body (tmhtml-with-file-initials x body))
	     (doc (list '!file body style lan
                        (url->string (get-texmacs-path)))))
	(texmacs->html doc opts))
      (begin
	(native-html-finalize (tmhtml-root x) (func? x '!file)))))
