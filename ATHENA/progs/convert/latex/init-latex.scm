
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-latex.scm
;; DESCRIPTION : setup latex converters
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert latex init-latex))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; LaTeX format
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (latex-recognizes-at? s pos)
  (set! pos (format-skip-spaces s pos))
  (cond ((format-test? s pos "\\document") #t)
        ((format-test? s pos "\\usepackage") #t)
        ((format-test? s pos "\\input") #t)
        ((format-test? s pos "\\includeonly") #t)
        ((format-test? s pos "\\chapter") #t)
        ((format-test? s pos "\\appendix") #t)
        ((format-test? s pos "\\section") #t)
        ((format-test? s pos "\\begin") #t)
        (else #f)))

(define (latex-recognizes? s)
  (and (string? s) (latex-recognizes-at? s 0)))

(define-format latex
  (:name "LaTeX")
  (:suffix "tex")
  (:recognize latex-recognizes?))

(define-format latex-class
  (:name "LaTeX class")
  (:suffix "ltx" "sty" "cls"))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; TeXmacs->LaTeX
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(lazy-define (convert latex texout) serialize-latex)
(lazy-define (convert latex tmtex) texmacs->latex)

(converter texmacs-stree latex-stree
  (:function-with-options texmacs->latex)
  (:option "texmacs->latex:source-tracking" "off")
  (:option "texmacs->latex:conservative" "on")
  (:option "texmacs->latex:transparent-source-tracking" "on")
  (:option "texmacs->latex:attach-tracking-info" "on")
  (:option "texmacs->latex:replace-style" "on")
  (:option "texmacs->latex:expand-macros" "on")
  (:option "texmacs->latex:expand-user-macros" "off")
  (:option "texmacs->latex:indirect-bib" "off")
  (:option "texmacs->latex:use-macros" "on")
  (:option "texmacs->latex:encoding" "utf-8")
  (:option "texmacs->latex:portable" "off"))

(converter latex-stree latex-document
  (:function serialize-latex))

(converter latex-stree latex-snippet
  (:function serialize-latex))

(tm-define (texmacs->latex-document x opts)
  (serialize-latex (texmacs->latex (tm->stree x) opts)))

(converter texmacs-stree latex-document
  (:function-with-options conservative-texmacs->latex)
  ;;(:function-with-options tracked-texmacs->latex)
  (:option "texmacs->latex:source-tracking" "off")
  (:option "texmacs->latex:conservative" "on")
  (:option "texmacs->latex:transparent-source-tracking" "on")
  (:option "texmacs->latex:attach-tracking-info" "on")
  (:option "texmacs->latex:replace-style" "on")
  (:option "texmacs->latex:expand-macros" "on")
  (:option "texmacs->latex:expand-user-macros" "off")
  (:option "texmacs->latex:indirect-bib" "off")
  (:option "texmacs->latex:use-macros" "on")
  (:option "texmacs->latex:encoding" "utf-8")
  (:option "texmacs->latex:portable" "off"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; LaTeX -> TeXmacs
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define latex-athena-data-search "ATHENA-DATA cmd=\"")
(define latex-athena-data-inline-prefix "\\INLINE_COMMENT{")
(define latex-athena-data-placeholder-prefix "ATHENAIMPORTOBJECT")
(define latex-athena-data-placeholder-suffix "X")

(define (latex-athena-data-warning msg)
  (display* "ATHENA-DATA warning: " msg "\n")
  (when (defined? 'set-message)
    (set-message msg "LaTeX import")))

(define (latex-athena-data-error msg)
  (display* "ATHENA-DATA error: " msg "\n")
  (when (defined? 'set-message)
    (set-message (string-append "Error: " msg) "LaTeX import")))

(define (latex-athena-data-string-find-char s pos c)
  (let loop ((i pos))
    (cond ((>= i (string-length s)) #f)
          ((char=? (string-ref s i) c) i)
          (else (loop (+ i 1))))))

(define (latex-athena-data-skip-spaces s pos)
  (let loop ((i pos))
    (if (and (< i (string-length s))
             (or (char=? (string-ref s i) #\space)
                 (char=? (string-ref s i) #\tab)))
        (loop (+ i 1))
        i)))

(define (latex-athena-data-line-start s pos)
  (let loop ((i (- pos 1)))
    (cond ((< i 0) 0)
          ((char=? (string-ref s i) #\newline) (+ i 1))
          (else (loop (- i 1))))))

(define (latex-athena-data-line-end s pos)
  (with i (string-search-forwards "\n" pos s)
    (if (< i 0) (string-length s) i)))

(define (latex-athena-data-after-line s pos)
  (with i (string-search-forwards "\n" pos s)
    (if (< i 0) (string-length s) (+ i 1))))

(define (latex-athena-data-inline-close s open)
  (let loop ((i (+ open 1)) (depth 1))
    (cond ((>= i (string-length s)) #f)
          ((and (char=? (string-ref s i) #\\)
                (< (+ i 1) (string-length s)))
           (loop (+ i 2) depth))
          ((char=? (string-ref s i) #\{)
           (loop (+ i 1) (+ depth 1)))
          ((char=? (string-ref s i) #\})
           (if (= depth 1) i (loop (+ i 1) (- depth 1))))
          (else (loop (+ i 1) depth)))))

(define (latex-athena-data-inline-record s pos)
  (let* ((prefix latex-athena-data-inline-prefix)
         (plen (string-length prefix))
         (start (- pos plen)))
    (and (>= start 0)
         (== (substring s start pos) prefix)
         (let* ((open (- pos 1))
                (close (latex-athena-data-inline-close s open)))
           (and close
                (list start (+ close 1) (substring s pos close)))))))

(define (latex-athena-data-comment-record s pos)
  (let* ((line-start (latex-athena-data-line-start s pos))
         (i (latex-athena-data-skip-spaces s line-start)))
    (and (< i (string-length s))
         (char=? (string-ref s i) #\%)
         (let ((j (latex-athena-data-skip-spaces s (+ i 1))))
           (and (= j pos)
                (list line-start
                      (latex-athena-data-after-line s pos)
                      (substring s pos (latex-athena-data-line-end s pos))))))))

(define (latex-athena-data-find-next s pos)
  (let loop ((i (string-search-forwards latex-athena-data-search pos s)))
    (if (< i 0) #f
        (or (latex-athena-data-inline-record s i)
            (latex-athena-data-comment-record s i)
            (loop (+ i 1))))))

(define (latex-athena-data-parse-quoted s pos)
  (and (< pos (string-length s))
       (char=? (string-ref s pos) #\")
       (let loop ((i (+ pos 1)) (cs '()))
         (cond ((>= i (string-length s)) #f)
               ((and (char=? (string-ref s i) #\\)
                     (< (+ i 1) (string-length s)))
                (loop (+ i 2) (cons (string-ref s (+ i 1)) cs)))
               ((char=? (string-ref s i) #\")
                (cons (list->string (reverse cs)) (+ i 1)))
               (else (loop (+ i 1) (cons (string-ref s i) cs)))))))

(define (latex-athena-data-parse-values s pos)
  (let loop ((i pos) (vals '()))
    (let ((j (latex-athena-data-skip-spaces s i)))
      (cond ((>= j (string-length s)) #f)
            ((char=? (string-ref s j) #\))
             (cons (reverse vals) (+ j 1)))
            ((char=? (string-ref s j) #\,)
             (loop (+ j 1) vals))
            ((char=? (string-ref s j) #\")
             (with q (latex-athena-data-parse-quoted s j)
               (and q (loop (cdr q) (cons (car q) vals)))))
            (else #f)))))

(define (latex-athena-data-parse-record body)
  (let* ((cmd-pos (string-search-forwards "cmd=\"" 0 body))
         (cmd-start (and (>= cmd-pos 0) (+ cmd-pos 5)))
         (cmd-end (and cmd-start
                       (latex-athena-data-string-find-char body cmd-start
                                                           #\")))
         (val-pos (and cmd-end
                       (string-search-forwards "val=(" cmd-end body)))
         (val-start (and val-pos (>= val-pos 0) (+ val-pos 5)))
         (vals (and val-start
                    (latex-athena-data-parse-values body val-start))))
    (and cmd-end vals
         (list (substring body cmd-start cmd-end) (car vals)))))

(define (latex-athena-data-version-parts s)
  (map (lambda (part)
         (let loop ((i 0))
           (if (and (< i (string-length part))
                    (char-numeric? (string-ref part i)))
               (loop (+ i 1))
               (if (= i 0) 0
                   (or (string->number (substring part 0 i)) 0)))))
       (string-tokenize-by-char s #\.)))

(define (latex-athena-data-version>? a b)
  (let loop ((x (latex-athena-data-version-parts a))
             (y (latex-athena-data-version-parts b)))
    (cond ((and (null? x) (null? y)) #f)
          ((null? x) (loop (list 0) y))
          ((null? y) (loop x (list 0)))
          ((> (car x) (car y)) #t)
          ((< (car x) (car y)) #f)
          (else (loop (cdr x) (cdr y))))))

(define (latex-athena-data-check-version vals)
  (when (nnull? vals)
    (with version (car vals)
      (when (latex-athena-data-version>? version (texmacs-version))
        (latex-athena-data-warning
         (string-append "file was exported by ATHENA " version
                        ", newer than this ATHENA " (texmacs-version)))))))

(define (latex-athena-data-placeholder nr)
  (string-append latex-athena-data-placeholder-prefix
                 (number->string nr)
                 latex-athena-data-placeholder-suffix))

(define (latex-athena-data-unwrap-snippet st)
  (if (func? st 'document 1) (cadr st) st))

(define (latex-athena-data-strip-inline-comment-definition s)
  (let* ((with-newline "\\long\\def\\INLINE_COMMENT#1{}\n")
         (without-newline "\\long\\def\\INLINE_COMMENT#1{}"))
    (string-replace (string-replace s with-newline "") without-newline "")))

(define (latex-athena-data-decode-object vals)
  (if (< (length vals) 2)
      (begin
        (latex-athena-data-warning "object record has too few values")
        #f)
      (let* ((len (string->number (car vals)))
             (payload (cadr vals)))
        (when (and len (!= len (string-length payload)))
          (latex-athena-data-warning
           (string-append "object payload length mismatch: expected "
                          (car vals) ", got "
                          (number->string (string-length payload)))))
        (catch #t
          (lambda ()
            (with raw (decode-base64 payload)
              (if (== raw "<error|compound athena-preserved-object>")
                  #f
                  (latex-athena-data-unwrap-snippet
                   (tree->stree (parse-texmacs-snippet raw))))))
          (lambda (key . args)
            (latex-athena-data-warning
             (string-append "could not decode object record: "
                            (object->string key)))
            #f)))))

(define (latex-athena-data-skip-id vals)
  (if (nnull? vals) (car vals) ""))

(define (latex-athena-data-pop-skip skip id)
  (cond ((null? skip)
         (latex-athena-data-error
          (string-append "skip_end without skip_begin: " id))
         skip)
        ((== (car skip) id) (cdr skip))
        ((in? id skip)
         (latex-athena-data-error
          (string-append "misnested skip_end: " id))
         (let loop ((rest skip))
           (cond ((null? rest) '())
                 ((== (car rest) id) (cdr rest))
                 (else (loop (cdr rest))))))
        (else
         (latex-athena-data-error
          (string-append "skip_end with unseen id: " id))
         skip)))

(define (latex-athena-data-preprocess s)
  (if (< (string-search-forwards latex-athena-data-search 0 s) 0)
      (list s '() '())
      (let ((serial 0)
            (saw-version? #f))
        (set! s (latex-athena-data-strip-inline-comment-definition s))
        (let loop ((pos 0) (out '()) (skip '()) (objects '()) (aux '()))
          (let ((rec (latex-athena-data-find-next s pos)))
            (if (not rec)
                (begin
                  (when (nnull? skip)
                    (latex-athena-data-error
                     (string-append "unterminated skip_begin: "
                                    (string-recompose (reverse skip) ", "))))
                  (list (string-concatenate
                         (reverse
                          (if (null? skip)
                              (cons (substring s pos (string-length s)) out)
                              out)))
                        (reverse objects)
                        (reverse aux)))
                (let* ((start (car rec))
                       (end (cadr rec))
                       (body (caddr rec))
                       (out* (if (null? skip)
                                 (cons (substring s pos start) out)
                                 out))
                       (parsed (latex-athena-data-parse-record body)))
                  (if (not parsed)
                      (begin
                        (latex-athena-data-warning
                         (string-append "invalid record: " body))
                        (loop end out* skip objects aux))
                      (let ((cmd (car parsed))
                            (vals (cadr parsed)))
                        (cond
                         ((== cmd "version")
                          (when (not saw-version?)
                            (set! saw-version? #t)
                            (latex-athena-data-check-version vals))
                          (loop end out* skip objects aux))
                         ((== cmd "aux")
                          (loop end out* skip objects
                                (if (null? skip) (cons vals aux) aux)))
                         ((== cmd "skip_begin")
                          (loop end out*
                                (cons (latex-athena-data-skip-id vals) skip)
                                objects aux))
                         ((== cmd "skip_end")
                          (loop end out*
                                (latex-athena-data-pop-skip
                                 skip (latex-athena-data-skip-id vals))
                                objects aux))
                         ((== cmd "object")
                          (if (nnull? skip)
                              (loop end out* skip objects aux)
                              (with obj (latex-athena-data-decode-object vals)
                                (if (not obj)
                                    (loop end out* skip objects aux)
                                    (let ((marker
                                           (latex-athena-data-placeholder
                                            serial)))
                                      (set! serial (+ serial 1))
                                      (loop end (cons marker out*) skip
                                            (cons (cons marker obj)
                                                  objects)
                                            aux))))))
                         (else
                          (latex-athena-data-warning
                           (string-append "unknown command: " cmd))
                          (loop end out* skip objects aux))))))))))))

(define (latex-athena-data-first-marker s objects)
  (let loop ((rest objects) (best #f))
    (if (null? rest) best
        (let* ((entry (car rest))
               (marker (car entry))
               (pos (string-search-forwards marker 0 s)))
          (if (or (< pos 0)
                  (and best (>= pos (cadr best))))
              (loop (cdr rest) best)
              (loop (cdr rest) (list marker pos (cdr entry))))))))

(define (latex-athena-data-replace-string s objects)
  (with hit (latex-athena-data-first-marker s objects)
    (if (not hit) s
        (let* ((marker (car hit))
               (pos (cadr hit))
               (obj (caddr hit))
               (end (+ pos (string-length marker)))
               (pre (substring s 0 pos))
               (post (substring s end (string-length s)))
               (post* (latex-athena-data-replace-string post objects))
               (parts (append (if (== pre "") '() (list pre))
                              (list obj)
                              (if (== post* "") '() (list post*)))))
          (if (list-1? parts) (car parts) `(concat ,@parts))))))

(define (latex-athena-data-replace-objects st objects)
  (cond ((null? objects) st)
        ((string? st) (latex-athena-data-replace-string st objects))
        ((list? st)
         (map (lambda (x) (latex-athena-data-replace-objects x objects)) st))
        (else st)))

(define (latex-athena-data-restore texmacs-tree objects)
  (if (null? objects) texmacs-tree
      (stree->tree
       (latex-athena-data-replace-objects (tree->stree texmacs-tree)
                                          objects))))

(define (latex-athena-data-normalize-legacy-tag tag)
  (cond ((== tag 'proofalternative) 'proof-alternative)
        ((== tag 'proofstandard) 'proof-standard)
        ((== tag 'proofof) 'proof-of)
        ((== tag 'renderproofalternative) 'render-proof-alternative)
        ((== tag 'renderproofstandard) 'render-proof-standard)
        (else tag)))

(define (latex-athena-data-normalize-legacy-tags texmacs-tree)
  (define (rewrite st)
    (cond ((list? st)
           (cons (latex-athena-data-normalize-legacy-tag (car st))
                 (map rewrite (cdr st))))
          (else st)))
  (stree->tree (rewrite (tree->stree texmacs-tree))))

(define (latex-athena-data-empty-tree? st)
  (cond ((string? st) (== st ""))
        ((list? st) (list-and (map latex-athena-data-empty-tree? (cdr st))))
        (else #f)))

(define (latex-athena-data-empty-doc-date? st)
  (and (func? st 'doc-date)
       (list-and (map latex-athena-data-empty-tree? (cdr st)))))

(define (latex-athena-data-strip-empty-doc-dates texmacs-tree)
  (define (rewrite st)
    (cond ((func? st 'doc-data)
           `(doc-data
             ,@(map rewrite
                    (list-filter (cdr st)
                                 (lambda (x)
                                   (not (latex-athena-data-empty-doc-date?
                                         x)))))))
          ((list? st) (map rewrite st))
          (else st)))
  (stree->tree (rewrite (tree->stree texmacs-tree))))

(define (latex-athena-data-aux-kind vals)
  (and (nnull? vals) (car vals)))

(define (latex-athena-data-aux? vals kind)
  (== (latex-athena-data-aux-kind vals) kind))

(define (latex-athena-data-aux-ref vals key)
  (let loop ((rest (cdr vals)))
    (cond ((or (null? rest) (null? (cdr rest))) #f)
          ((== (car rest) key) (cadr rest))
          (else (loop (cddr rest))))))

(define (latex-athena-data-list-ref-default l nr def)
  (if (> (length l) nr) (list-ref l nr) def))

(define (latex-athena-data-strip-quotes s)
  (if (and (string? s)
           (>= (string-length s) 2)
           (char=? (string-ref s 0) #\")
           (char=? (string-ref s (- (string-length s) 1)) #\"))
      (substring s 1 (- (string-length s) 1))
      s))

(define (latex-athena-data-decode-aux-tree vals)
  (if (< (length vals) 3)
      #f
      (let* ((len (string->number (cadr vals)))
             (payload (caddr vals)))
        (when (and len (!= len (string-length payload)))
          (latex-athena-data-warning
           (string-append "aux payload length mismatch: expected "
                          (cadr vals) ", got "
                          (number->string (string-length payload)))))
        (catch #t
          (lambda ()
            (latex-athena-data-unwrap-snippet
             (tree->stree (parse-texmacs-snippet (decode-base64 payload)))))
          (lambda (key . args)
            (latex-athena-data-warning
             (string-append "could not decode aux record: "
                            (object->string key)))
            #f)))))

(define (latex-athena-data-first-aux-tree aux kind)
  (let loop ((rest aux))
    (cond ((null? rest) #f)
          ((latex-athena-data-aux? (car rest) kind)
           (latex-athena-data-decode-aux-tree (car rest)))
          (else (loop (cdr rest))))))

(define (latex-athena-data-image-aux-list aux)
  (list-filter aux (lambda (vals)
                     (latex-athena-data-aux? vals "img_size"))))

(define (latex-athena-data-sized-image image vals)
  (let* ((args (cdr image))
         (path (latex-athena-data-strip-quotes
                (latex-athena-data-list-ref-default args 0 "")))
         (old-width (latex-athena-data-list-ref-default args 1 ""))
         (old-height (latex-athena-data-list-ref-default args 2 ""))
         (old-x (latex-athena-data-list-ref-default args 3 ""))
         (old-y (latex-athena-data-list-ref-default args 4 ""))
         (width (or (latex-athena-data-aux-ref vals "width") old-width))
         (height (or (latex-athena-data-aux-ref vals "height") old-height)))
    `(image ,path ,width ,height ,old-x ,old-y)))

(define (latex-athena-data-find-image st)
  (cond ((func? st 'image) st)
        ((list? st)
         (let loop ((rest (cdr st)))
           (cond ((null? rest) #f)
                 ((latex-athena-data-find-image (car rest))
                  => identity)
                 (else (loop (cdr rest))))))
        (else #f)))

(define (latex-athena-data-apply-image-aux texmacs-tree aux)
  (let ((pending (latex-athena-data-image-aux-list aux)))
    (define (consume)
      (if (null? pending) #f
          (let ((next (car pending)))
            (set! pending (cdr pending))
            next)))
    (define (rewrite st)
      (cond ((func? st 'image)
             (with vals (consume)
               (if vals (latex-athena-data-sized-image st vals) st)))
            ((or (func? st 'resizebox) (func? st 'scalebox))
             (with image (latex-athena-data-find-image st)
               (if image
                   (with vals (consume)
                     (if vals (latex-athena-data-sized-image image vals)
                         (map rewrite st)))
                   (map rewrite st))))
            ((list? st) (map rewrite st))
            (else st)))
    (if (null? pending) texmacs-tree
        (stree->tree (rewrite (tree->stree texmacs-tree))))))

(define (latex-athena-data-apply-document-aux texmacs-tree aux)
  (let* ((style (latex-athena-data-first-aux-tree aux "document_style"))
         (initial (latex-athena-data-first-aux-tree aux "document_initial"))
         (st (tree->stree texmacs-tree)))
    (when (and style (tmfile? st))
      (set! st (tmfile-assign st 'style style)))
    (when (and initial (tmfile? st))
      (set! st (tmfile-assign st 'initial initial)))
    (stree->tree st)))

(tm-define (latex-document->texmacs x . opts)
  (if (list-1? opts) (set! opts (car opts)))
  (let* ((as-pic (== (get-preference "latex->texmacs:fallback-on-pictures")
                     "on"))
         (data (latex-athena-data-preprocess x))
         (latex (car data))
         (objects (cadr data))
         (aux (caddr data))
         (parsed (conservative-latex->texmacs latex as-pic))
         (restored (latex-athena-data-restore parsed objects))
         (normalized
          (latex-athena-data-normalize-legacy-tags restored))
         (without-empty-dates
          (latex-athena-data-strip-empty-doc-dates normalized))
         (with-images
          (latex-athena-data-apply-image-aux without-empty-dates aux)))
    (latex-athena-data-apply-document-aux with-images aux)))

(converter latex-document latex-tree
  (:function parse-latex-document))

(converter latex-snippet latex-tree
  (:function parse-latex))

(converter latex-document texmacs-tree
  (:function-with-options latex-document->texmacs)
  (:option "latex->texmacs:fallback-on-pictures" "on")
  (:option "latex->texmacs:source-tracking" "off")
  (:option "latex->texmacs:conservative" "off")
  (:option "latex->texmacs:transparent-source-tracking" "off"))

(converter latex-class-document texmacs-tree
  (:function latex-class-document->texmacs))

(converter latex-tree texmacs-tree
  (:function latex->texmacs))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Tests
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(lazy-define (convert latex test-tmtex) test-tmtex)
