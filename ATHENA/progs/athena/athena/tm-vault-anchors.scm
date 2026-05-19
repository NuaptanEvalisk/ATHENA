(texmacs-module (athena athena tm-vault-anchors)
  (:use (kernel boot abbrevs)
        (kernel boot ahash-table)
        (kernel library base)
        (kernel library list)
        (kernel library tree)
        (kernel athena tm-define)
        (utils library cursor)))

(define-preferences
  ("vault auto anchor enunciations on save" "off" noop))

(define vault-anchor-enunciation-tags
  '(theorem lemma corollary proposition axiom definition notation convention
    conjecture law remark note example warning disambiguation acknowledgments
    exercise problem question solution answer proof proof-alternative
    proof-standard proof-of quote-env))

(define vault-anchor-parenthesized-title-tags
  '(theorem lemma proposition corollary conjecture question))

(define vault-anchor-direct-title-tags
  '(definition notation convention axiom law remark note example warning
    disambiguation acknowledgments exercise problem solution answer quote-env))

(define (vault-anchor-stree-func? t tag)
  (and (pair? t) (eq? (car t) tag)))

(define (vault-anchor-stree-compound? t)
  (and (pair? t) (symbol? (car t))))

(define (vault-anchor-tree-tag t)
  (and (vault-anchor-stree-compound? t) (car t)))

(define (vault-anchor-enunciation? t)
  (and-with tag (vault-anchor-tree-tag t)
    (in? tag vault-anchor-enunciation-tags)))

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

(define (vault-anchor-common-title-candidate? s)
  (in? (vault-anchor-normalize-title-candidate s)
       '("not" "cannot" "however" "but" "should not" "is" "is not"
         "can not")))

(define (vault-anchor-join-text parts)
  (vault-anchor-collapse-whitespace
   (string-join (list-filter parts (lambda (s) (!= s ""))) " ")))

(define (vault-anchor-plain-text t)
  (cond ((string? t) t)
        ((not (pair? t)) "")
        ((in? (car t) '(label reference pageref image include bibliography
                        transclude TRANSCLUDE))
         "")
        ((and (eq? (car t) 'hlink) (pair? (cdr t)))
         (vault-anchor-plain-text (cadr t)))
        (else
         (vault-anchor-join-text (map vault-anchor-plain-text (cdr t))))))

(define (vault-anchor-first-strong-text t)
  (cond ((not (pair? t)) #f)
        ((and (eq? (car t) 'strong) (pair? (cdr t)))
         (vault-anchor-plain-text (cadr t)))
        (else
         (let loop ((l (cdr t)))
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

(define (vault-anchor-enunciation-body t)
  (cond ((and (vault-anchor-stree-func? t 'proof-of) (>= (length t) 3))
         (caddr t))
        ((and (pair? t) (pair? (cdr t))) (cadr t))
        (else "")))

(define (vault-anchor-title-from-enunciation t)
  (let* ((tag (vault-anchor-tree-tag t))
         (body (vault-anchor-enunciation-body t))
         (strong (or (vault-anchor-first-strong-text body) "")))
    (cond ((and (eq? tag 'proof-of) (>= (length t) 3))
           (vault-anchor-sanitize-text (vault-anchor-plain-text (cadr t)) 80))
          ((in? tag vault-anchor-parenthesized-title-tags)
           (vault-anchor-sanitize-text
            (vault-anchor-parenthesized-title strong) 100))
          ((in? tag vault-anchor-direct-title-tags)
           (if (or (== strong "")
                   (vault-anchor-common-title-candidate? strong))
               ""
               (vault-anchor-sanitize-text strong 100)))
          (else ""))))

(define (vault-anchor-prefix tag title)
  (cond ((eq? tag 'proof-of) (if (== title "") "proof" (string-append "proof:" title)))
        (else (symbol->string tag))))

(define (vault-anchor-id-for-enunciation t)
  (let* ((tag (vault-anchor-tree-tag t))
         (title (vault-anchor-title-from-enunciation t))
         (body (vault-anchor-enunciation-body t))
         (sample (vault-anchor-sanitize-text
                  (vault-anchor-plain-text body) 100))
         (prefix (vault-anchor-prefix tag title)))
    (cond ((and (!= title "") (not (eq? tag 'proof-of)))
           (string-append (symbol->string tag) ":" title))
          ((and (eq? tag 'proof-of) (!= title "") (!= sample ""))
           (string-append prefix " " sample))
          ((!= sample "")
           (string-append prefix ":" sample))
          (else prefix))))

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
  (when (< (length (vector-ref summary 2)) 8)
    (vector-set! summary 2 (append (vector-ref summary 2) (list message)))))

(define (vault-anchor-has-wrapper? previous next)
  (and previous next
       (vault-anchor-upper-label? previous)
       (vault-anchor-lower-label? next)))

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
  (let loop ((rest children) (previous #f) (out '()))
    (cond
      ((not (pair? rest))
       (reverse out))
      (else
       (let ((current (car rest))
             (tail (cdr rest)))
         (cond
           ((vault-anchor-ignorable? current)
            (loop tail previous (cons current out)))
           ((and (vault-anchor-upper-label? current)
                 (vault-anchor-lower-label?
                  (vault-anchor-next-substantive tail)))
            (let skip ((scan tail))
              (cond
                ((not (pair? scan))
                 (loop scan previous out))
                ((vault-anchor-ignorable? (car scan))
                 (skip (cdr scan)))
                ((vault-anchor-lower-label? (car scan))
                 (vault-anchor-summary-add! summary 1 1)
                 (vault-anchor-summary-note!
                  summary
                  (string-append "remove dead anchors: "
                                 (vault-anchor-label-key
                                  (vault-anchor-label-text current))))
                 (loop (cdr scan) previous out))
                (else
                 (loop tail current (cons current out))))))
           ((vault-anchor-enunciation? current)
            (let ((next (vault-anchor-next-substantive tail)))
              (if (vault-anchor-has-wrapper? previous next)
                  (loop tail current (cons current out))
                  (let* ((raw-id (vault-anchor-id-for-enunciation current))
                         (id (vault-anchor-unique-id raw-id counts))
                         (upper `(label ,(string-append id " {")))
                         (lower `(label ,(string-append id " }"))))
                    (vault-anchor-summary-add! summary 0 1)
                    (vault-anchor-summary-note!
                     summary
                     (string-append "wrap "
                                    (symbol->string
                                     (vault-anchor-tree-tag current))
                                    ": " id))
                    (if dry-run?
                        (loop tail current (cons current out))
                        (loop tail lower
                              (cons lower
                                    (cons current
                                          (cons upper out)))))))))
           (else
            (loop tail current (cons current out)))))))))

(define (vault-anchor-live-node-stree parent index)
  (tree->stree (tree-child-ref parent index)))

(define (vault-anchor-live-next-substantive-index parent start)
  (let ((n (tree-arity parent)))
    (let loop ((i start))
      (cond ((>= i n) #f)
            ((vault-anchor-ignorable?
              (vault-anchor-live-node-stree parent i))
             (loop (+ i 1)))
            (else i)))))

(define (vault-anchor-path-desc? p q)
  (cond ((and (null? p) (null? q)) #f)
        ((null? p) #f)
        ((null? q) #t)
        ((> (car p) (car q)) #t)
        ((< (car p) (car q)) #f)
        (else (vault-anchor-path-desc? (cdr p) (cdr q)))))

(define (vault-anchor-op-before? a b)
  (let ((pa (vector-ref a 2))
        (pb (vector-ref b 2))
        (ia (vector-ref a 3))
        (ib (vector-ref b 3)))
    (if (equal? pa pb)
        (> ia ib)
        (vault-anchor-path-desc? pa pb))))

(define (vault-anchor-document-live-ops parent path counts summary)
  (let loop ((i 0) (previous #f) (ops '()))
    (cond
      ((>= i (tree-arity parent)) ops)
      (else
       (let ((current (vault-anchor-live-node-stree parent i)))
         (cond
           ((vault-anchor-ignorable? current)
            (loop (+ i 1) previous ops))
           ((and (vault-anchor-upper-label? current)
                 (and-with next-i
                   (vault-anchor-live-next-substantive-index parent (+ i 1))
                   (vault-anchor-lower-label?
                    (vault-anchor-live-node-stree parent next-i))))
            (let ((next-i (vault-anchor-live-next-substantive-index
                           parent (+ i 1))))
              (vault-anchor-summary-add! summary 1 1)
              (vault-anchor-summary-note!
               summary
               (string-append "remove dead anchors: "
                              (vault-anchor-label-key
                               (vault-anchor-label-text current))))
              (loop (+ next-i 1)
                    previous
                    (cons (vector 'remove parent path i (+ 1 (- next-i i)))
                          ops))))
           ((vault-anchor-enunciation? current)
            (let ((next (and-with next-i
                          (vault-anchor-live-next-substantive-index
                           parent (+ i 1))
                          (vault-anchor-live-node-stree parent next-i))))
              (if (vault-anchor-has-wrapper? previous next)
                  (loop (+ i 1) current ops)
                  (let* ((raw-id (vault-anchor-id-for-enunciation current))
                         (id (vault-anchor-unique-id raw-id counts))
                         (upper `(label ,(string-append id " {")))
                         (lower `(label ,(string-append id " }"))))
                    (vault-anchor-summary-add! summary 0 1)
                    (vault-anchor-summary-note!
                     summary
                     (string-append "wrap "
                                    (symbol->string
                                     (vault-anchor-tree-tag current))
                                    ": " id))
                    (loop (+ i 1)
                          current
                          (cons (vector 'wrap parent path i upper lower)
                                ops))))))
           (else
            (loop (+ i 1) current ops))))))))

(define (vault-anchor-live-ops node path counts summary)
  (if (not (tree-compound? node))
      '()
      (let ((ops '()))
        (let loop ((i 0))
          (when (< i (tree-arity node))
            (set! ops
                  (append
                   (vault-anchor-live-ops
                    (tree-child-ref node i) (append path (list i))
                    counts summary)
                   ops))
            (loop (+ i 1))))
        (when (== (tree-label node) 'document)
          (set! ops
                (append
                 (vault-anchor-document-live-ops node path counts summary)
                 ops)))
        ops)))

(define (vault-anchor-live-plan body)
  (let* ((stree (tree->stree body))
         (counts (make-ahash-table))
         (summary (vector 0 0 '())))
    (vault-anchor-register-existing-labels stree counts)
    (let ((ops (vault-anchor-live-ops body '() counts summary)))
      (vector summary ops))))

(define (vault-anchor-plan body)
  (vector-ref (vault-anchor-live-plan body) 0))

(define (vault-anchor-current-buffer? buf)
  (and (current-buffer)
       (== (url->url (current-buffer)) (url->url buf))))

(define (vault-anchor-prepare-live-edit! buf)
  (when (vault-anchor-current-buffer? buf)
    (when (selection-active-any?)
      (selection-cancel))
    (go-start)))

(define (vault-anchor-apply! buf)
  (let* ((body (buffer-get-body buf))
         (plan (vault-anchor-live-plan body))
         (summary (vector-ref plan 0))
         (ops (sort (vector-ref plan 1) vault-anchor-op-before?)))
    (when (not (null? ops))
      (vault-anchor-prepare-live-edit! buf)
      (for-each
       (lambda (op)
         (let ((kind (vector-ref op 0))
               (parent (vector-ref op 1))
               (index (vector-ref op 3)))
           (cond
             ((eq? kind 'remove)
              (tree-remove parent index (vector-ref op 4)))
             ((eq? kind 'wrap)
              (tree-var-insert
               parent (+ index 1)
               (cons 'tuple (list (stree->tree (vector-ref op 5)))))
              (tree-var-insert
               parent index
               (cons 'tuple (list (stree->tree (vector-ref op 4)))))))))
       ops))
    summary))

(define (vault-anchor-summary-empty? summary)
  (and (== (vector-ref summary 0) 0)
       (== (vector-ref summary 1) 0)))

(define (vault-anchor-truncate-line s limit)
  (if (<= (string-length s) limit)
      s
      (string-append (substring s 0 (- limit 3)) "...")))

(define (vault-anchor-summary-print summary)
  (display* "ATHENA] anchor enunciations dry-run: wrap "
            (number->string (vector-ref summary 0))
            " enunciation(s), remove "
            (number->string (vector-ref summary 1))
            " dead anchor pair(s)\n")
  (for-each (lambda (note)
              (display* "ATHENA]   " note "\n"))
            (vector-ref summary 2)))

(define (vault-anchor-summary-message summary action)
  (let* ((wraps (number->string (vector-ref summary 0)))
         (dead (number->string (vector-ref summary 1)))
         (notes (map (cut vault-anchor-truncate-line <> 72)
                     (vector-ref summary 2)))
         (head (string-append "Anchor enunciations dry-run\n\n"
                              "Wrap enunciations: " wraps "\n"
                              "Remove dead anchor pairs: " dead))
         (tail (if (null? notes) ""
                   (string-append "\n\nExamples:\n- "
                                  (string-join notes "\n- ")
                                  "\n\nFull dry-run summary was printed to the console."))))
    (string-append head tail "\n\n" action)))

(define (vault-anchor-current-buffer-supported? buf)
  (and buf
       (buffer-exists? buf)
       (not (url-scratch? buf))
       (in? (url-suffix buf) '("ath" "tm" "ts" "tp" "stm" "tmml" "scm" ""))))

(tm-define (vault-anchor-enunciations-confirmed buf cont)
  (if (not (buffer-exists? buf))
      (begin
        (set-message "Buffer no longer exists" "Anchor enunciations")
        (when cont (cont)))
      (with-buffer buf
        (let ((summary (vault-anchor-apply! buf)))
          (cond ((vault-anchor-summary-empty? summary)
                 (set-message "No enunciation anchors needed"
                              "Anchor enunciations"))
                (else
                 (set-message
                  (string-append "Wrapped "
                                 (number->string (vector-ref summary 0))
                                 " enunciation(s); removed "
                                 (number->string (vector-ref summary 1))
                                 " dead anchor pair(s)")
                  "Anchor enunciations")))
          (when cont (cont))))))

(tm-define (vault-anchor-enunciations buf . maybe-cont)
  (:interactive #t)
  (let ((cont (and (pair? maybe-cont) (car maybe-cont))))
    (cond ((not (vault-anchor-current-buffer-supported? buf))
           (set-message "Current buffer cannot be anchored" "Anchor enunciations")
           (when cont (cont)))
          (else
           (let ((summary (vault-anchor-plan (buffer-get-body buf))))
             (vault-anchor-summary-print summary)
             (if (vault-anchor-summary-empty? summary)
                 (begin
                   (set-message "No enunciation anchors needed"
                                "Anchor enunciations")
                   (when cont (cont)))
                 (user-confirm
                  (vault-anchor-summary-message
                   summary
                   "Apply these anchor changes?")
                  #f
                  (lambda (answ)
                    (if answ
                        (vault-anchor-enunciations-confirmed buf cont)
                        (when cont (cont)))))))))))

(tm-define (anchor-enunciations-current-document)
  (:interactive #t)
  (vault-anchor-enunciations (current-buffer)))

(tm-define (vault-auto-anchor-before-save? buf)
  (and (== (get-preference "vault auto anchor enunciations on save") "on")
       (vault-anchor-current-buffer-supported? buf)))

(tm-define (vault-anchor-before-manual-save buf cont)
  (if (vault-auto-anchor-before-save? buf)
      (vault-anchor-enunciations buf cont)
      (cont)))

(tm-widget (vault-anchor-preferences-widget)
  (aligned
    (item (text "Auto anchor enunciations on manual save:")
      (toggle (set-preference "vault auto anchor enunciations on save"
                              (if answer "on" "off"))
              (equal? (get-preference
                       "vault auto anchor enunciations on save")
                      "on")))))
