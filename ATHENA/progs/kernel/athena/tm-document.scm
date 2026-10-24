;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document field helpers (not format conversion or converter registration).
;; GNU GPL version 3 or later; see LICENSE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel athena tm-document)
  (:use (kernel athena tm-define) (kernel athena tm-modes)))
(import-from (kernel athena tm-preferences))

(define (tmfile-pair? t)
  (and (tm-compound? t)
       (nnull? (tm-children t))))

(define-public (tmfile-get doc what)
  (and (tm-func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with val (assoc-ref (map tm->list (tm-children doc)) what)
         (if (pair? val) (set! val (car val)))
         val)))

(define-public (tmfile-set doc what val)
  (and (tm-func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with l (reverse (map tm->list (tm-children doc)))
         (set! l (assoc-set! l what (list val)))
         (cons 'document (reverse l)))))

(define-public (tmfile? doc)
  (and (tmfile-get doc 'body) #t))

(define-public (tmfile-extract doc what)
  (if (tree? doc) (set! doc (tree->stree doc)))
  (and (func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with val (assoc-ref (cdr doc) what)
         (if (pair? val) (set! val (car val)))
         (if (tree? val) (set! val (tree->stree val)))
         val)))

(define-public (tmfile-assign doc what val)
  (if (tree? doc) (set! doc (tree->stree doc)))
  (and (func? doc 'document)
       (list-and (map tmfile-pair? (tm-children doc)))
       (with l (reverse (cdr doc))
         (set! l (assoc-set! l what (list (tm->tree val))))
         (cons 'document (reverse l)))))

(define (default-init var)
  (cond ((== var "mode") "text")
        ((== var "language") "english")
        (else "")))

(tm-define (tmfile-init doc var . explicit?)
  (with init (tmfile-extract doc 'initial)
    (if (not init)
        (and (null? explicit?) (default-init var))
        (with item (list-find (cdr init) (lambda (x) (== (cadr x) var)))
          (if item (caddr item)
              (and (null? explicit?) (default-init var)))))))

(tm-define (tmfile-style-list doc)
  (with style (tmfile-extract doc 'style)
    (cond ((tm-func? style 'tuple) (cdr style))
          ((string? style) (list style))
          (else (list)))))

(tm-define (tmfile-language doc)
  (let* ((style (tmfile-style-list doc))
         (lans (list-intersection style supported-languages))
         (lan (tmfile-init doc "language" #t)))
    (cond (lan lan)
          ((nnull? lans) (car lans))
          (else "english"))))
