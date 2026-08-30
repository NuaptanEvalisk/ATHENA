(texmacs-module (athena athena tm-vault-anchors)
  (:use (kernel boot abbrevs)
        (kernel boot ahash-table)
        (kernel library base)
        (kernel library list)
        (kernel library tree)
        (kernel athena tm-convert)
        (kernel athena tm-define)
        (utils library cursor)))


(define vault-anchor-enunciation-tags
  '(theorem lemma corollary proposition axiom definition notation convention
    conjecture law remark note example warning disambiguation acknowledgments
    exercise problem question solution solution* answer proof proof-alternative
    proof-standard proof-of quote-env render-theorem render-remark
    render-exercise render-solution render-proof render-proof-alternative
    render-proof-standard))

(define vault-anchor-parenthesized-title-tags
  '(theorem lemma proposition corollary conjecture question))

(define vault-anchor-direct-title-tags
  '(definition notation convention axiom law remark note example warning
    disambiguation acknowledgments exercise problem solution answer quote-env))

(define vault-anchor-proof-tags
  '(proof proof-alternative proof-standard proof-of solution solution* render-proof
    render-proof-alternative render-proof-standard render-solution))

(define vault-anchor-separated-proof-owner-tags
  '(theorem lemma corollary proposition axiom definition conjecture remark
    law example question render-theorem render-remark render-exercise))

(define (vault-anchor-heading-level tag)
  (cond ((or (eq? tag 'section) (eq? tag 'section*)) 1)
        ((or (eq? tag 'subsection) (eq? tag 'subsection*)) 2)
        ((or (eq? tag 'subsubsection) (eq? tag 'subsubsection*)) 3)
        ((or (eq? tag 'paragraph) (eq? tag 'paragraph*)) 4)
        ((or (eq? tag 'subparagraph) (eq? tag 'subparagraph*)) 5)
        (else 0)))

(define (vault-anchor-stree-func? t tag)
  (and (pair? t) (eq? (car t) tag)))

(define (vault-anchor-stree-compound? t)
  (and (pair? t) (symbol? (car t))))

(define (vault-anchor-tree-tag t)
  (and (vault-anchor-stree-compound? t) (car t)))

(define (vault-anchor-base-tag tag)
  (cond ((eq? tag 'render-theorem) 'theorem)
        ((eq? tag 'render-remark) 'remark)
        ((eq? tag 'render-exercise) 'exercise)
        ((eq? tag 'render-solution) 'solution)
        ((eq? tag 'render-proof) 'proof)
        ((eq? tag 'render-proof-alternative) 'proof-alternative)
        ((eq? tag 'render-proof-standard) 'proof-standard)
        (else tag)))

(define (vault-anchor-enunciation? t)
  (and-with tag (vault-anchor-tree-tag t)
    (in? tag vault-anchor-enunciation-tags)))

(define (vault-anchor-heading? t)
  (and-with tag (vault-anchor-tree-tag t)
    (and (> (vault-anchor-heading-level tag) 0)
         (pair? (cdr t))
         (!= (vault-anchor-heading-title t) ""))))

(define (vault-anchor-label? t)
  (and (vault-anchor-stree-func? t 'label)
       (pair? (cdr t))
       (string? (cadr t))))

(define (vault-anchor-label-text t)
  (if (vault-anchor-label? t) (cadr t) ""))

(define (vault-anchor-label-key label)
  (let ((n (string-length label)))
    (if (and (>= n 2)
             (== (substring label (- n 2) n) " {"))
        (substring label 0 (- n 2))
        (if (and (>= n 2)
                 (== (substring label (- n 2) n) " }"))
            (substring label 0 (- n 2))
            label))))

(define (vault-anchor-upper-label? t)
  (and (vault-anchor-label? t)
       (string-contains? (vault-anchor-label-text t) "{")))

(define (vault-anchor-lower-label? t)
  (and (vault-anchor-label? t)
       (string-contains? (vault-anchor-label-text t) "}")))

(define (vault-anchor-heading-label? t level)
  (and (vault-anchor-label? t)
       (string-starts? (vault-anchor-label-text t)
                       (string-append "H" (number->string level) " "))))

(define (vault-anchor-wrapper-key previous next)
  (and previous next
       (vault-anchor-upper-label? previous)
       (vault-anchor-lower-label? next)
       (let ((upper (vault-anchor-label-key
                     (vault-anchor-label-text previous)))
             (lower (vault-anchor-label-key
                     (vault-anchor-label-text next))))
         (if (== upper lower) upper #f))))

(define (vault-anchor-ignorable? t)
  (and (string? t) (== (tm-string-trim-both t) "")))

(define (vault-anchor-ascii-alnum? c)
  (let ((n (char->integer c)))
    (or (and (>= n (char->integer #\0)) (<= n (char->integer #\9)))
        (and (>= n (char->integer #\A)) (<= n (char->integer #\Z)))
        (and (>= n (char->integer #\a)) (<= n (char->integer #\z))))))

(define (vault-anchor-cjk? c)
  (let ((n (char->integer c)))
    (or (and (>= n #x3400) (<= n #x4DBF))
        (and (>= n #x4E00) (<= n #x9FFF))
        (and (>= n #xF900) (<= n #xFAFF))
        (and (>= n #x20000) (<= n #x2A6DF))
        (and (>= n #x2A700) (<= n #x2B73F))
        (and (>= n #x2B740) (<= n #x2B81F))
        (and (>= n #x2B820) (<= n #x2CEAF))
        (and (>= n #x2CEB0) (<= n #x2EBEF))
        (and (>= n #x30000) (<= n #x3134F)))))

(define (vault-anchor-collapse-whitespace s)
  (let loop ((chars (string->list s)) (out '()) (space? #t))
    (cond ((null? chars)
           (tm-string-trim-both (list->string (reverse out))))
          ((tm-char-whitespace? (car chars))
           (if space?
               (loop (cdr chars) out #t)
               (loop (cdr chars) (cons #\space out) #t)))
          (else
           (loop (cdr chars) (cons (car chars) out) #f)))))

(define (vault-anchor-sanitize-text s limit)
  (let loop ((chars (string->list (vault-anchor-collapse-whitespace s)))
             (out '()) (count 0) (space? #t))
    (cond ((or (null? chars) (and (> limit 0) (>= count limit)))
           (tm-string-trim-both (list->string (reverse out))))
          ((tm-char-whitespace? (car chars))
           (if space?
               (loop (cdr chars) out count #t)
               (loop (cdr chars) (cons #\space out) (+ count 1) #t)))
          ((or (vault-anchor-ascii-alnum? (car chars))
               (vault-anchor-cjk? (car chars)))
           (loop (cdr chars) (cons (car chars) out) (+ count 1) #f))
          (else
           (loop (cdr chars) out count space?)))))

(define (vault-anchor-downcase-ascii s)
  (list->string
   (map (lambda (c)
          (let ((n (char->integer c)))
            (if (and (>= n (char->integer #\A))
                     (<= n (char->integer #\Z)))
                (integer->char (+ n 32))
                c)))
        (string->list s))))

(define (vault-anchor-normalize-title-candidate s)
  (vault-anchor-downcase-ascii
   (vault-anchor-sanitize-text s 0)))

(define vault-anchor-title-filter-cache-root #f)
(define vault-anchor-title-filter-cache '())

(tm-define (vault-anchor-title-filter-invalidate)
  (set! vault-anchor-title-filter-cache-root #f)
  (set! vault-anchor-title-filter-cache '()))

(define (vault-anchor-title-filter-root)
  (if (and (defined? 'vault-active?) (vault-active?))
      (vault-get-root)
      (url-none)))

(define (vault-anchor-title-filter)
  (let ((root (vault-anchor-title-filter-root)))
    (when (or (not vault-anchor-title-filter-cache-root)
              (!= root vault-anchor-title-filter-cache-root))
      (set! vault-anchor-title-filter-cache-root root)
      (set! vault-anchor-title-filter-cache
            (map vault-anchor-normalize-title-candidate
                 (artifact-title-filter-read root))))
    vault-anchor-title-filter-cache))

(define (vault-anchor-common-title-candidate? s)
  (in? (vault-anchor-normalize-title-candidate s)
       (vault-anchor-title-filter)))

(define (vault-anchor-join-text parts)
  (vault-anchor-collapse-whitespace
   (string-join (list-filter parts (lambda (s) (!= s ""))) " ")))

(define (vault-anchor-last l)
  (and (pair? l)
       (if (null? (cdr l)) (car l) (vault-anchor-last (cdr l)))))

(define (vault-anchor-format-wrapper? tag)
  (in? tag '(with style-with)))

(define (vault-anchor-name=? x name)
  (cond ((string? x) (== x name))
        ((symbol? x) (== (symbol->string x) name))
        (else #f)))

(define (vault-anchor-bold-value? x)
  (or (vault-anchor-name=? x "bold")
      (vault-anchor-name=? x "bold-series")))

(define (vault-anchor-font-series-key? x)
  (or (vault-anchor-name=? x "font-series")
      (vault-anchor-name=? x "fontseries")))

(define (vault-anchor-bold-format-wrapper? t)
  (and (pair? t)
       (vault-anchor-format-wrapper? (vault-anchor-tree-tag t))
       (let loop ((l (cdr t)))
         (cond ((or (null? l) (null? (cdr l))) #f)
               ((and (vault-anchor-font-series-key? (car l))
                     (vault-anchor-bold-value? (cadr l)))
                #t)
               (else (loop (cddr l)))))))

(define (vault-anchor-visible-children t)
  (let ((tag (vault-anchor-tree-tag t)))
    (cond ((not tag) '())
          ((vault-anchor-format-wrapper? tag)
           (let ((children (cdr t)))
             ;; Formatting wrappers store property/value pairs before the
             ;; rendered body.  Anchor text must use only that body.
             (if (>= (length children) 3)
                 (list (vault-anchor-last children))
                 '())))
          ((and (eq? tag 'hlink) (pair? (cdr t)))
           (list (cadr t)))
          (else (cdr t)))))

(define (vault-anchor-plain-text t)
  (cond ((string? t) t)
        ((not (pair? t)) "")
        ((in? (car t) '(label reference pageref image include
                        transclude TRANSCLUDE))
         "")
        (else
         (vault-anchor-join-text
          (map vault-anchor-plain-text
               (vault-anchor-visible-children t))))))

(define (vault-anchor-heading-title t)
  (if (and (pair? t) (pair? (cdr t)))
      (vault-anchor-collapse-whitespace (vault-anchor-plain-text (cadr t)))
      ""))

(define (vault-anchor-first-strong-text t)
  (cond ((not (pair? t)) #f)
        ((and (eq? (car t) 'strong) (pair? (cdr t)))
         (vault-anchor-plain-text (cadr t)))
        ((vault-anchor-bold-format-wrapper? t)
         (vault-anchor-plain-text (vault-anchor-last (cdr t))))
        (else
         (let loop ((l (vault-anchor-visible-children t)))
           (cond ((null? l) #f)
                 ((vault-anchor-first-strong-text (car l)) => identity)
                 (else (loop (cdr l))))))))

(define (vault-anchor-parenthesized-title s)
  (let ((s (tm-string-trim-both s)))
    (if (and (> (string-length s) 2)
             (string-starts? s "(")
             (string-contains? s ")"))
        (let loop ((i 1))
          (cond ((>= i (string-length s)) "")
                ((== (string-ref s i) #\))
                 (tm-string-trim-both (substring s 1 i)))
                (else (loop (+ i 1)))))
        "")))

(define (vault-anchor-parenthesized-title-from-tree t)
  (let* ((strong (or (vault-anchor-first-strong-text t) ""))
         (strong-parenthesized (vault-anchor-parenthesized-title strong)))
    (if (!= strong-parenthesized "")
        strong-parenthesized
        (vault-anchor-parenthesized-title (vault-anchor-plain-text t)))))

(define (vault-anchor-enunciation-body t)
  (cond ((and (in? (vault-anchor-tree-tag t)
                   '(render-theorem render-remark render-exercise
                     render-solution render-proof render-proof-alternative
                     render-proof-standard))
              (>= (length t) 3))
         (caddr t))
        ((and (vault-anchor-stree-func? t 'proof-of) (>= (length t) 3))
         (caddr t))
        ((and (pair? t) (pair? (cdr t))) (cadr t))
        (else "")))

(define (vault-anchor-render-title-tree t)
  (if (and (in? (vault-anchor-tree-tag t)
                '(render-theorem render-remark render-exercise
                  render-solution render-proof render-proof-alternative
                  render-proof-standard))
           (>= (length t) 3))
      (cadr t)
      ""))

(define (vault-anchor-render-title t)
  (vault-anchor-sanitize-text
   (vault-anchor-plain-text (vault-anchor-render-title-tree t)) 100))

(define (vault-anchor-title-from-enunciation t)
  (let* ((tag (vault-anchor-base-tag (vault-anchor-tree-tag t)))
         (render-title-tree (vault-anchor-render-title-tree t))
         (render-title (vault-anchor-render-title t))
         (body (vault-anchor-enunciation-body t))
         (strong (or (vault-anchor-first-strong-text body) ""))
         (render-parenthesized
          (vault-anchor-parenthesized-title-from-tree render-title-tree))
         (body-parenthesized
          (vault-anchor-parenthesized-title-from-tree body))
         (parenthesized (if (!= render-parenthesized "")
                            render-parenthesized
                            body-parenthesized))
         (strong-title (if (or (== strong "")
                               (vault-anchor-common-title-candidate? strong))
                           ""
                           (vault-anchor-sanitize-text strong 100))))
    (cond ((and (eq? tag 'proof-of) (>= (length t) 3))
           (vault-anchor-sanitize-text (vault-anchor-plain-text (cadr t)) 80))
          ((in? tag vault-anchor-parenthesized-title-tags)
           (if (!= parenthesized "")
               (vault-anchor-sanitize-text parenthesized 100)
               (if (!= strong-title "") strong-title render-title)))
          ((!= render-title "")
           render-title)
          ((in? tag vault-anchor-direct-title-tags)
           strong-title)
          (else ""))))

(define (vault-anchor-prefix tag title)
  (let ((base (vault-anchor-base-tag tag)))
    (cond ((eq? base 'proof-of)
           (if (== title "") "proof" (string-append "proof:" title)))
          (else (symbol->string base)))))

(define (vault-anchor-proof? t)
  (and-with tag (vault-anchor-tree-tag t)
    (in? tag vault-anchor-proof-tags)))

(define (vault-anchor-separated-proof-owner? t)
  (and-with tag (vault-anchor-tree-tag t)
    (in? tag vault-anchor-separated-proof-owner-tags)))

(define (vault-anchor-separated-proof-prefix t)
  (let* ((tag (vault-anchor-base-tag (vault-anchor-tree-tag t)))
         (title (vault-anchor-title-from-enunciation t)))
    (cond ((or (eq? tag 'proof-alternative)
               (eq? tag 'proof-standard))
           (symbol->string tag))
          ((or (eq? tag 'solution) (eq? tag 'solution*))
           "solution")
          ((eq? tag 'proof-of)
           (if (== title "") "proof" (string-append "proof:" title)))
          ((and (!= title "") (eq? tag 'proof))
           (string-append "proof:" title))
          (else "proof"))))

(define (vault-anchor-enunciation-suffix id)
  (let ((pos (string-search-forwards ":" 0 id)))
    (if (>= pos 0)
        (substring id (+ pos 1) (string-length id))
        id)))

(define (vault-anchor-proof-context-id proof context-id)
  (string-append (vault-anchor-separated-proof-prefix proof)
                 ":"
                 (vault-anchor-enunciation-suffix context-id)))

(define (vault-anchor-id-for-enunciation t . maybe-proof-context)
  (let* ((tag (vault-anchor-base-tag (vault-anchor-tree-tag t)))
         (title (vault-anchor-title-from-enunciation t))
         (body (vault-anchor-enunciation-body t))
         (sample (vault-anchor-sanitize-text
                  (vault-anchor-plain-text body) 100))
         (prefix (vault-anchor-prefix tag title))
         (proof-context (and (pair? maybe-proof-context)
                             (car maybe-proof-context))))
    (cond ((and proof-context (vault-anchor-proof? t))
           (vault-anchor-proof-context-id t proof-context))
          ((and (!= title "") (not (vault-anchor-proof? t)))
           (string-append (symbol->string tag) ":" title))
          ((and (vault-anchor-proof? t) (!= sample ""))
           (let ((proof-prefix (vault-anchor-separated-proof-prefix t)))
             (string-append proof-prefix
                            (if (string-contains? proof-prefix ":") " " ":")
                            sample)))
          ((and (eq? tag 'proof-of) (!= title "") (!= sample ""))
           (string-append prefix " " sample))
          ((!= sample "")
           (string-append prefix ":" sample))
          (else prefix))))

(define (vault-anchor-id-for-heading t)
  (let* ((tag (vault-anchor-tree-tag t))
         (level (vault-anchor-heading-level tag))
         (title (vault-anchor-heading-title t)))
    (string-append "H" (number->string level) " " title)))

(define (vault-anchor-register-existing-labels t counts)
  (cond ((vault-anchor-label? t)
         (let ((key (vault-anchor-label-key (vault-anchor-label-text t))))
           (when (not (ahash-ref counts key))
             (ahash-set! counts key 1))))
        ((pair? t)
         (for-each (cut vault-anchor-register-existing-labels <> counts)
                   (cdr t)))))

(define (vault-anchor-unique-id id counts)
  (let ((count (or (ahash-ref counts id) 0)))
    (ahash-set! counts id (+ count 1))
    (if (== count 0)
        id
        (string-append id " (" (number->string count) ")"))))

(define (vault-anchor-summary-add! summary key value)
  (vector-set! summary key (+ (vector-ref summary key) value)))

(define (vault-anchor-summary-note! summary message)
  (vector-set! summary 2 (append (vector-ref summary 2) (list message))))

(define (vault-anchor-summary-new)
  (vector 0 0 '() 0 0 '()))

(define (vault-anchor-summary-rename! summary old-label new-label)
  (when (and (!= old-label "") (!= new-label "")
             (!= old-label new-label))
    (vector-set! summary 5
                 (append (vector-ref summary 5)
                         (list (list old-label new-label))))))

(define (vault-anchor-summary-update! summary message)
  (vault-anchor-summary-add! summary 4 1)
  (vault-anchor-summary-note! summary message))

(define (vault-anchor-has-heading-anchor? previous current)
  (and previous
       (vault-anchor-heading? current)
       (vault-anchor-heading-label?
        previous
        (vault-anchor-heading-level (vault-anchor-tree-tag current)))))

(define (vault-anchor-has-wrapper? previous next)
  (and (vault-anchor-wrapper-key previous next) #t))

(define (vault-anchor-replace-last-substantive out replacement)
  (cond ((null? out) out)
        ((vault-anchor-ignorable? (car out))
         (cons (car out)
               (vault-anchor-replace-last-substantive (cdr out)
                                                      replacement)))
        (else (cons replacement (cdr out)))))

(define (vault-anchor-drop-through-next-substantive l)
  (cond ((null? l) l)
        ((vault-anchor-ignorable? (car l))
         (vault-anchor-drop-through-next-substantive (cdr l)))
        (else (cdr l))))

(define (vault-anchor-existing-heading-label previous current)
  (and (vault-anchor-has-heading-anchor? previous current)
       (vault-anchor-label-text previous)))

(define (vault-anchor-existing-wrapper-key previous next)
  (vault-anchor-wrapper-key previous next))

(define (vault-anchor-wrapper-label id suffix)
  `(label ,(string-append id suffix)))

(define (vault-anchor-decimal-string? s)
  (and (> (string-length s) 0)
       (let loop ((i 0))
         (cond ((>= i (string-length s)) #t)
               ((let* ((c (string-ref s i))
                       (n (char->integer c)))
                  (and (>= n (char->integer #\0))
                       (<= n (char->integer #\9))))
                (loop (+ i 1)))
               (else #f)))))

(define (vault-anchor-generated-suffix? existing raw)
  (let* ((prefix (string-append raw " ("))
         (prefix-n (string-length prefix))
         (existing-n (string-length existing)))
    (and (string-starts? existing prefix)
         (> existing-n prefix-n)
         (== (substring existing (- existing-n 1) existing-n) ")")
         (vault-anchor-decimal-string?
          (substring existing prefix-n (- existing-n 1))))))

(define (vault-anchor-compatible-id? existing raw)
  (or (== existing raw)
      (vault-anchor-generated-suffix? existing raw)))

(define (vault-anchor-rename-wrapper! summary old-id new-id)
  (vault-anchor-summary-rename! summary
                                (string-append old-id " {")
                                (string-append new-id " {"))
  (vault-anchor-summary-rename! summary
                                (string-append old-id " }")
                                (string-append new-id " }")))

(define (vault-anchor-next-substantive l)
  (cond ((null? l) #f)
        ((vault-anchor-ignorable? (car l))
         (vault-anchor-next-substantive (cdr l)))
        (else (car l))))

(define (vault-anchor-transform-tree t counts summary dry-run?)
  (cond ((not (pair? t)) t)
        ((eq? (car t) 'document)
         (cons 'document
               (vault-anchor-transform-document-children
                (map (cut vault-anchor-transform-tree <> counts summary dry-run?)
                     (cdr t))
                counts summary dry-run?)))
        (else
         (cons (car t)
               (map (cut vault-anchor-transform-tree <> counts summary dry-run?)
                    (cdr t))))))

(define (vault-anchor-transform-document-children children counts summary dry-run?)
  (let loop ((rest children) (previous #f) (proof-context-id #f) (out '()))
    (cond
      ((not (pair? rest))
       (reverse out))
      (else
       (let ((current (car rest))
             (tail (cdr rest)))
         (cond
           ((vault-anchor-ignorable? current)
            (loop tail previous proof-context-id (cons current out)))
           ((and (vault-anchor-upper-label? current)
                 (vault-anchor-lower-label?
                  (vault-anchor-next-substantive tail)))
            (let skip ((scan tail))
              (cond
                ((not (pair? scan))
                 (loop scan previous proof-context-id out))
                ((vault-anchor-ignorable? (car scan))
                 (skip (cdr scan)))
                ((vault-anchor-lower-label? (car scan))
                 (vault-anchor-summary-add! summary 1 1)
                 (vault-anchor-summary-note!
                  summary
                  (string-append "remove dead anchors: "
                                 (vault-anchor-label-key
                                  (vault-anchor-label-text current))))
                 (loop (cdr scan) previous proof-context-id out))
                (else
                 (loop tail current proof-context-id (cons current out))))))
           ((vault-anchor-label? current)
            (loop tail current proof-context-id (cons current out)))
           ((vault-anchor-enunciation? current)
            (let* ((next (vault-anchor-next-substantive tail))
                   (proof-context (and (vault-anchor-proof? current)
                                       proof-context-id))
                   (raw-id (if proof-context
                               (vault-anchor-id-for-enunciation
                                current proof-context)
                               (vault-anchor-id-for-enunciation current)))
                   (wrapper-id (vault-anchor-existing-wrapper-key
                                previous next)))
              (if wrapper-id
                  (if (vault-anchor-compatible-id? wrapper-id raw-id)
                      (let ((kept-owner-id
                             (and (vault-anchor-separated-proof-owner? current)
                                  wrapper-id)))
                        (loop tail current
                              (and (not (vault-anchor-proof? current))
                                   kept-owner-id)
                              (cons current out)))
                      (let* ((id (vault-anchor-unique-id raw-id counts))
                             (upper (vault-anchor-wrapper-label id " {"))
                             (lower (vault-anchor-wrapper-label id " }"))
                             (new-tail (vault-anchor-drop-through-next-substantive
                                        tail))
                             (new-out (vault-anchor-replace-last-substantive
                                       out upper)))
                        (vault-anchor-summary-update!
                         summary
                         (string-append "update "
                                        (symbol->string
                                         (vault-anchor-tree-tag current))
                                        " anchor: "
                                        wrapper-id " -> " id))
                        (vault-anchor-rename-wrapper! summary wrapper-id id)
                        (if dry-run?
                            (loop tail current
                                  (if (vault-anchor-separated-proof-owner? current)
                                      id
                                      #f)
                                  (cons current out))
                            (loop new-tail lower
                                  (if (vault-anchor-separated-proof-owner? current)
                                      id
                                      #f)
                                  (cons lower
                                        (cons current new-out))))))
                  (let* ((raw-id raw-id)
                         (id (vault-anchor-unique-id raw-id counts))
                         (upper (vault-anchor-wrapper-label id " {"))
                         (lower (vault-anchor-wrapper-label id " }"))
                         (new-owner-id (if (vault-anchor-separated-proof-owner?
                                            current)
                                           id
                                           #f)))
                    (vault-anchor-summary-add! summary 0 1)
                    (vault-anchor-summary-note!
                     summary
                     (string-append "wrap "
                                    (symbol->string
                                     (vault-anchor-tree-tag current))
                                    ": " id))
                    (if dry-run?
                        (loop tail current new-owner-id (cons current out))
                        (loop tail lower
                              new-owner-id
                              (cons lower
                                    (cons current
                                          (cons upper out)))))))))
           ((vault-anchor-heading? current)
            (let* ((raw-id (vault-anchor-id-for-heading current))
                   (existing (vault-anchor-existing-heading-label previous
                                                                   current)))
              (cond ((and existing
                          (vault-anchor-compatible-id? existing raw-id))
                     (loop tail current #f (cons current out)))
                    (existing
                     (let* ((id (vault-anchor-unique-id raw-id counts))
                            (label `(label ,id))
                            (new-out (vault-anchor-replace-last-substantive
                                      out label)))
                       (vault-anchor-summary-update!
                        summary
                        (string-append "update heading anchor: "
                                       existing " -> " id))
                       (vault-anchor-summary-rename! summary existing id)
                       (if dry-run?
                           (loop tail current #f (cons current out))
                           (loop tail current #f
                                 (cons current new-out)))))
                    (else
                     (let* ((id (vault-anchor-unique-id raw-id counts))
                            (label `(label ,id)))
                       (vault-anchor-summary-add! summary 3 1)
                       (vault-anchor-summary-note!
                        summary
                        (string-append "anchor heading: " id))
                       (if dry-run?
                           (loop tail current #f
                                 (cons current out))
                           (loop tail current #f
                                 (cons current
                                       (cons label out)))))))))
           (else
            (loop tail current #f (cons current out)))))))))

(define (vault-anchor-plan body)
  (let* ((st (tree->stree body))
         (counts (make-ahash-table))
         (summary (vault-anchor-summary-new)))
    (vault-anchor-register-existing-labels st counts)
    (vault-anchor-transform-tree st counts summary #t)
    summary))

(define (vault-anchor-transform-stree st)
  (let* ((counts (make-ahash-table))
         (summary (vault-anchor-summary-new)))
    (vault-anchor-register-existing-labels st counts)
    (let ((new-st (vault-anchor-transform-tree st counts summary #f)))
      (list summary new-st))))

(define (vault-anchor-transform-document doc)
  (let* ((st (tree->stree doc))
         (body (tmfile-extract st 'body)))
    (if body
        (let* ((res (vault-anchor-transform-stree body))
               (summary (car res))
               (new-body (cadr res))
               (new-st (if (vault-anchor-summary-empty? summary)
                           st
                           (tmfile-assign st 'body new-body))))
          (list summary (stree->tree new-st)))
        (let* ((res (vault-anchor-transform-stree st))
               (summary (car res))
               (new-st (cadr res)))
          (list summary (stree->tree new-st))))))

(define (vault-anchor-tab-safe s)
  (string-replace (string-replace s "\t" " ") "\n" " "))

(define vault-anchor-rename-separator
  (list->string (list (integer->char 30))))

(define vault-anchor-rename-field-separator
  (list->string (list (integer->char 31))))

(define (vault-anchor-renames-string summary)
  (string-join
   (map (lambda (entry)
          (string-append (vault-anchor-tab-safe (car entry))
                         vault-anchor-rename-field-separator
                         (vault-anchor-tab-safe (cadr entry))))
        (vector-ref summary 5))
   vault-anchor-rename-separator))

(define (vault-anchor-maintenance-result status wraps dead headings updates
                                         changed message renames)
  (string-append status "\t"
                 (number->string wraps) "\t"
                 (number->string dead) "\t"
                 (number->string headings) "\t"
                 (number->string updates) "\t"
                 (if changed "1" "0") "\t"
                 (vault-anchor-tab-safe message) "\t"
                 renames))

(tm-define (vault-anchor-maintenance-file u)
  (catch #t
    (lambda ()
      (let ((doc (tree-import u "texmacs")))
        (if (== doc (tm->tree "error"))
            (vault-anchor-maintenance-result "error" 0 0 0 0 #f
                                             "could not import document" "")
            (let* ((res (vault-anchor-transform-document doc))
                   (summary (car res))
                   (new-doc (cadr res))
                   (wraps (vector-ref summary 0))
                   (dead (vector-ref summary 1))
                   (headings (vector-ref summary 3))
                   (updates (vector-ref summary 4))
                   (renames (vault-anchor-renames-string summary)))
              (if (vault-anchor-summary-empty? summary)
                  (vault-anchor-maintenance-result "ok" 0 0 0 0 #f "" "")
                  (if (tree-export new-doc u "texmacs")
                      (vault-anchor-maintenance-result
                       "error" wraps dead headings updates #f
                       "could not export document" "")
                      (vault-anchor-maintenance-result
                       "ok" wraps dead headings updates #t "" renames)))))))
    (lambda args
      (vault-anchor-maintenance-result "error" 0 0 0 0 #f
                                       (object->string args) ""))))

(tm-define (vault-anchor-maintenance-check-file u)
  (catch #t
    (lambda ()
      (let ((doc (tree-import u "texmacs")))
        (if (== doc (tm->tree "error"))
            (vault-anchor-maintenance-result "error" 0 0 0 0 #f
                                             "could not import document" "")
            (let* ((res (vault-anchor-transform-document doc))
                   (summary (car res))
                   (wraps (vector-ref summary 0))
                   (dead (vector-ref summary 1))
                   (headings (vector-ref summary 3))
                   (updates (vector-ref summary 4)))
              (vault-anchor-maintenance-result
               "ok" wraps dead headings updates
               (not (vault-anchor-summary-empty? summary)) "" "")))))
    (lambda args
      (vault-anchor-maintenance-result "error" 0 0 0 0 #f
                                       (object->string args) ""))))

(define (vault-anchor-current-buffer? buf)
  (and (current-buffer)
       (== (url->url (current-buffer)) (url->url buf))))

(define (vault-anchor-update-map-for-buffer! buf summary)
  (when (and (defined? 'vault-active?)
             (vault-active?)
             (not (null? (vector-ref summary 5))))
    (catch #t
      (lambda ()
        (let* ((root (url-append (vault-get-root) ""))
               (rel-path (url->unix (url-delta root buf)))
               (renames (vault-anchor-renames-string summary)))
          (when (not (string-starts? rel-path "../"))
            (vault-rewrite-anchor-references rel-path renames))))
      (lambda args (noop)))))

(define (vault-anchor-prepare-live-edit! buf sx sy)
  (when (vault-anchor-current-buffer? buf)
    (when (selection-active-any?)
      (selection-cancel))
    (go-start)
    (set-scroll sx sy)))

(define (vault-anchor-restore-position! buf pos sx sy)
  (when (and pos (vault-anchor-current-buffer? buf))
    (catch #t
      (lambda ()
        (go-to (position-get pos)))
      (lambda args (noop))))
  (when (vault-anchor-current-buffer? buf)
    (set-scroll sx sy)))

(define (vault-anchor-schedule-restore! buf pos sx sy)
  (vault-anchor-restore-position! buf pos sx sy)
  (delayed (:idle 1) (vault-anchor-restore-position! buf pos sx sy))
  (delayed (:idle 25) (vault-anchor-restore-position! buf pos sx sy))
  (delayed (:idle 100)
    (begin
      (vault-anchor-restore-position! buf pos sx sy)
      (when pos (position-delete pos)))))

(define (vault-anchor-capture-position buf)
  (if (vault-anchor-current-buffer? buf)
      (let ((pos (position-new)))
        (position-set pos (cursor-path))
        (list pos (get-scroll-x) (get-scroll-y)))
      (list #f 0 0)))

(define (vault-anchor-apply! buf)
  (let* ((restore (vault-anchor-capture-position buf))
         (pos (car restore))
         (sx (cadr restore))
         (sy (caddr restore))
         (body (buffer-get-body buf))
         (res (vault-anchor-transform-stree (tree->stree body)))
         (summary (car res))
         (new-body (cadr res)))
    (if (vault-anchor-summary-empty? summary)
        (when pos (position-delete pos))
        (begin
          (vault-anchor-prepare-live-edit! buf sx sy)
          (buffer-set-body buf (stree->tree new-body))
          (vault-anchor-update-map-for-buffer! buf summary)
          (when pos
            (vault-anchor-schedule-restore! buf pos sx sy))))
    summary))

(define (vault-anchor-apply-before-save! buf)
  (let* ((restore (vault-anchor-capture-position buf))
         (pos (car restore))
         (sx (cadr restore))
         (sy (caddr restore))
         (body (buffer-get-body buf))
         (res (vault-anchor-transform-stree (tree->stree body)))
         (summary (car res))
         (new-body (cadr res)))
    (if (vault-anchor-summary-empty? summary)
        (when pos (position-delete pos))
        (begin
          (buffer-set-body buf (stree->tree new-body))
          (vault-anchor-update-map-for-buffer! buf summary)
          (when pos
            (vault-anchor-schedule-restore! buf pos sx sy))))
    summary))

(define (vault-anchor-summary-empty? summary)
  (and (== (vector-ref summary 0) 0)
       (== (vector-ref summary 1) 0)
       (== (vector-ref summary 3) 0)
       (== (vector-ref summary 4) 0)))

(define (vault-anchor-truncate-line s limit)
  (if (<= (string-length s) limit)
      s
      (string-append (substring s 0 (- limit 3)) "...")))

(define (vault-anchor-summary-print summary)
  (display* "ATHENA] anchor structures dry-run: wrap "
            (number->string (vector-ref summary 0))
            " enunciation(s), remove "
            (number->string (vector-ref summary 1))
            " dead anchor pair(s), add "
            (number->string (vector-ref summary 3))
            " heading anchor(s), update "
            (number->string (vector-ref summary 4))
            " stale anchor structure(s)\n")
  (for-each (lambda (note)
              (display* "ATHENA]   " note "\n"))
            (vector-ref summary 2)))

(define (vault-anchor-summary-message summary action)
  (let* ((wraps (number->string (vector-ref summary 0)))
         (dead (number->string (vector-ref summary 1)))
         (headings (number->string (vector-ref summary 3)))
         (updates (number->string (vector-ref summary 4)))
         (notes (map (cut vault-anchor-truncate-line <> 72)
                     (vector-ref summary 2)))
         (head (string-append "Anchor structures dry-run\n\n"
                              "Wrap enunciations: " wraps "\n"
                              "Add heading anchors: " headings "\n"
                              "Remove dead anchor pairs: " dead "\n"
                              "Update stale anchors: " updates))
         (tail (if (null? notes) ""
                   (string-append "\n\nExamples:\n- "
                                  (string-join notes "\n- ")
                                  "\n\nFull dry-run summary was printed to the console."))))
    (string-append head tail "\n\n" action)))

(define (vault-anchor-summary-notes-string summary)
  (string-join
   (map (cut vault-anchor-truncate-line <> 180)
        (vector-ref summary 2))
   "<<<ATHENA-ANCHOR-ACTION>>>"))

(define (vault-anchor-confirm-native summary)
  (native-anchor-enunciations-confirm
   (number->string (vector-ref summary 0))
   (number->string (vector-ref summary 1))
   (number->string (vector-ref summary 3))
   (vault-anchor-summary-notes-string summary)))

(define (vault-anchor-current-buffer-supported? buf)
  (and buf
       (buffer-exists? buf)
       (not (url-scratch? buf))
       (in? (url-suffix buf) '("ath" "tm" "ts" "tp" "stm" "tmml" "scm" ""))))

(tm-define (vault-anchor-enunciations-confirmed buf cont)
  (if (not (buffer-exists? buf))
      (begin
        (set-message "Buffer no longer exists" "Anchor structures")
        (when cont (cont)))
      (with-buffer buf
        (let ((summary (vault-anchor-apply! buf)))
          (cond ((vault-anchor-summary-empty? summary)
                 (set-message "No structural anchors needed"
                              "Anchor structures"))
                (else
                 (set-message
                  (string-append "Wrapped "
                                 (number->string (vector-ref summary 0))
                                 " enunciation(s); added "
                                 (number->string (vector-ref summary 3))
                                 " heading anchor(s); removed "
                                 (number->string (vector-ref summary 1))
                                 " dead anchor pair(s); updated "
                                 (number->string (vector-ref summary 4))
                                 " stale anchor structure(s)")
                  "Anchor structures")))
          (when cont (cont))))))

(tm-define (vault-anchor-enunciations-confirmed-before-save buf cont)
  (if (not (buffer-exists? buf))
      (begin
        (set-message "Buffer no longer exists" "Anchor structures")
        (when cont (cont)))
      (with-buffer buf
        (let ((summary (vault-anchor-apply-before-save! buf)))
          (cond ((vault-anchor-summary-empty? summary)
                 (set-message "No structural anchors needed"
                              "Anchor structures"))
                (else
                 (set-message
                  (string-append "Wrapped "
                                 (number->string (vector-ref summary 0))
                                 " enunciation(s); added "
                                 (number->string (vector-ref summary 3))
                                 " heading anchor(s); removed "
                                 (number->string (vector-ref summary 1))
                                 " dead anchor pair(s); updated "
                                 (number->string (vector-ref summary 4))
                                 " stale anchor structure(s)")
                  "Anchor structures")))
          (when cont (cont))))))

(tm-define (vault-anchor-enunciations buf . maybe-cont)
  (:interactive #t)
  (let ((cont (and (pair? maybe-cont) (car maybe-cont))))
    (cond ((not (vault-anchor-current-buffer-supported? buf))
           (set-message "Current buffer cannot be anchored" "Anchor structures")
           (when cont (cont)))
          (else
           (let ((summary (vault-anchor-plan (buffer-get-body buf))))
             (vault-anchor-summary-print summary)
             (if (vault-anchor-summary-empty? summary)
                 (begin
                   (set-message "No structural anchors needed"
                                "Anchor structures")
                   (when cont (cont)))
                 (if (vault-anchor-confirm-native summary)
                     (vault-anchor-enunciations-confirmed buf cont)
                     (when cont (cont)))))))))

(tm-define (anchor-enunciations-current-document)
  (:interactive #t)
  (vault-anchor-enunciations (current-buffer)))

(tm-define (vault-anchor-enunciations-before-save buf cont)
  (cond ((not (vault-anchor-current-buffer-supported? buf))
         (set-message "Current buffer cannot be anchored" "Anchor structures")
         (when cont (cont)))
        (else
         (let ((summary (vault-anchor-plan (buffer-get-body buf))))
           (vault-anchor-summary-print summary)
           (if (vault-anchor-summary-empty? summary)
               (begin
                 (set-message "No structural anchors needed"
                              "Anchor structures")
                 (when cont (cont)))
               (if (or (== (get-preference
                             "vault auto approve anchor changes") "on")
                       (vault-anchor-confirm-native summary))
                   (vault-anchor-enunciations-confirmed-before-save buf cont)
                   (when cont (cont))))))))

(tm-define (vault-auto-anchor-before-save? buf)
  (and (== (get-preference "vault auto anchor enunciations on save") "on")
       (vault-anchor-current-buffer-supported? buf)))

(tm-define (vault-anchor-before-manual-save buf cont)
  (if (vault-auto-anchor-before-save? buf)
      (vault-anchor-enunciations-before-save buf cont)
      (cont)))

(tm-widget (vault-anchor-preferences-widget)
  (aligned
    (item (text "Auto anchor structures on manual save:")
      (toggle (set-preference "vault auto anchor enunciations on save"
                              (if answer "on" "off"))
              (equal? (get-preference
                       "vault auto anchor enunciations on save")
                      "on")))
    (item (text "Automatically approve anchor changes on manual save:")
      (toggle (set-preference "vault auto approve anchor changes"
                              (if answer "on" "off"))
              (equal? (get-preference
                       "vault auto approve anchor changes")
                      "on")))))
