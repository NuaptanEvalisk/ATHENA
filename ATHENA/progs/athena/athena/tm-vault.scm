(texmacs-module (athena athena tm-vault)
  (:use (kernel boot abbrevs)
        (kernel library list)
        (kernel library tree)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)
        (utils library cursor)
        (generic document-edit)))
(import-from (kernel athena tm-preferences))


(define-secure-symbols wikilink-repair-apply vault-transclude-repair
                       node-reference-jump-to-source
                       vault-jump-to-source artifact-jump-to-position
                       artifact-navigation-failed
                       load-buffer load-vault-dir
                       string->url vault-load-latest-action
                       vault-validate-root-namespace
                       go-to-system-welcome-page
                       go-to-welcome-page
                       go-to-vault-initial-page
                       load-help-article
                       load-help-buffer
                       vault-show-explorer
                       vault-show-explorer-and-track
                       vault-explorer-track-file
                       open-namespace-manager
                       open-namespace-explorer
                       namespace-new-file-within-wizard
                       namespace-info-page
                       open-quick-switcher
                       new-document)

(define (vault-url-component-unreserved? c)
  (or (char-alphabetic? c)
      (char-numeric? c)
      (char=? c #\-)
      (char=? c #\.)
      (char=? c #\_)
      (char=? c #\~)))

(define (vault-url-hex-char n)
  (integer->char (if (< n 10) (+ n 48) (+ n 55))))

(define (vault-url-hex-value c)
  (cond ((char-numeric? c) (- (char->integer c) 48))
        ((and (char>=? c #\A) (char<=? c #\F))
         (- (char->integer c) 55))
        ((and (char>=? c #\a) (char<=? c #\f))
         (- (char->integer c) 87))
        (else #f)))

(define (vault-url-component-encode s)
  (list->string
   (append-map
    (lambda (c)
      (let ((n (char->integer c)))
        (if (or (>= n 128) (vault-url-component-unreserved? c))
            (list c)
            (list #\%
                  (vault-url-hex-char (quotient n 16))
                  (vault-url-hex-char (remainder n 16))))))
    (string->list s))))

(define (vault-url-component-decode s)
  (let loop ((cs (string->list s)) (out '()))
    (if (null? cs)
        (list->string (reverse out))
        (if (and (char=? (car cs) #\%)
                 (pair? (cdr cs))
                 (pair? (cddr cs)))
            (let ((hi (vault-url-hex-value (cadr cs)))
                  (lo (vault-url-hex-value (caddr cs))))
              (if (and hi lo)
                  (loop (cdddr cs)
                        (cons (integer->char (+ (* 16 hi) lo)) out))
                  (loop (cdr cs) (cons (car cs) out))))
            (loop (cdr cs) (cons (car cs) out))))))

(define (vault-wikilink-url uuid)
  (string-append "tmfs://wikilink/"
                 (vault-url-component-encode uuid)))

(define (vault-node-model-active?)
  (>= (vault-get-node-model-version) 1))

(define (vault-tmfs-navigation-name u protocol)
  (when (string? u) (set! u (system->url u)))
  (let* ((text (url->unix (url-unroot u)))
         (prefix (string-append protocol "/")))
    (and (string-starts? text prefix)
         (string-drop text (string-length prefix)))))

(define (vault-wikilink-navigation-url? u)
  (when (string? u) (set! u (system->url u)))
  (or (url-rooted-tmfs-protocol? u "wikilink")
      (url-rooted-tmfs-protocol? u "Wikilink")))

(define (vault-wikilink-navigation-name u)
  (or (vault-tmfs-navigation-name u "wikilink")
      (vault-tmfs-navigation-name u "Wikilink")
      ""))

(define (vault-wikilink-repair-choice-url bad-uuid new-uuid path anchor)
  (string-append "tmfs://wikilink-repair-apply/"
                 (vault-url-component-encode bad-uuid) "/"
                 (vault-url-component-encode new-uuid) "/"
                 (vault-url-component-encode path) "/"
                 (vault-url-component-encode anchor)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Settings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (get-fuzzy-limit)
  (let ((pref (get-preference "vault fuzzy search limit")))
    (or (string->number pref) 3)))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Fuzzy Search Logic
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (string-fuzzy-match? s hint)
  (let ((s (cond ((url? s) (url->unix s))
                 ((tree? s) (tree->string s))
                 (else s)))
        (hint (cond ((url? hint) (url->unix hint))
                    ((tree? hint) (tree->string hint))
                    (else hint))))
    (if (string-null? hint) #t
        (string-occurs? (string-downcase hint) (string-downcase s)))))

(define (vault-scan-files dir hint)
  (if (gui-interrupted?) '()
      (begin
        (display* "  Scanning: " dir " for " hint "\n")
        (refresh-now "wikilink-search")
        (let* ((all (append (url-read-directory dir "*.tm")
                            (url-read-directory dir "*.ath")))
               (matches (list-filter all (lambda (u) (string-fuzzy-match? (url-tail u) hint))))
               (subdirs (list-filter (url-read-directory dir "*") url-directory?)))
          (for (d subdirs)
            (let ((name (url->unix (url-tail d))))
              (if (and (not (gui-interrupted?)) (not (string-starts? name ".")))
                  (set! matches (append matches (vault-scan-files d hint))))))
          matches))))

(define (vault-find-anchor-in-file u hint)
  (if (string-null? hint) ""
      (begin
        (display* "    Searching anchors in " u "\n")
        (let* ((t (tree-import u "texmacs"))
               (match #f)
               (pred? (lambda (node)
                        (and (tree-compound? node)
                             (== (tree-label node) 'label)
                             (>= (tree-arity node) 1)
                             (string-fuzzy-match? (tree->string (tree-ref node 0)) hint)))))
          (tree-search t (lambda (node)
                           (if (and (not match) (pred? node))
                               (set! match (tree->string (tree-ref node 0))))
                           #f))
          (if match
              (begin (display* "      Found anchor: " match "\n") match)
              "")))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Wikilink Handler
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(define (wikilink-name-parts name)
  (let* ((parts (string-tokenize-by-char name #\/))
         (uuid (if (pair? parts) (vault-url-component-decode (car parts)) ""))
         (file-hint (if (and (pair? parts) (pair? (cdr parts)))
                        (vault-url-component-decode (cadr parts)) ""))
         (anchor-hint (if (and (pair? parts) (pair? (cdr parts)) (pair? (cddr parts)))
                          (vault-url-component-decode (list->tmfs (cddr parts))) "")))
    (list uuid file-hint anchor-hint)))

(define (wikilink-navigation-target name)
  (with (uuid file-hint anchor-hint) (wikilink-name-parts name)
    (let ((node (vault-get-node uuid)))
      (and (tree? node) (== (tree-label node) 'tuple)
           (let* ((rel-path (tree->string (tree-ref node 0)))
                  (a-end (tree->string (tree-ref node 2)))
                  (abs-url (url-append (vault-get-root) (unix->url rel-path))))
             (and (url-exists? abs-url) (list abs-url a-end)))))))

(define (wikilink-repair-handler-sub name)
  (with (uuid file-hint anchor-hint) (wikilink-name-parts name)
    (if (vault-node-model-active?)
        `(document
           (style (tuple "generic"))
           (body
             (document
               (bold "Broken source UUID: ") ,uuid
               " -- file and anchor hints are not identity in a migrated Vault.")))
        (wikilink-trigger-repair uuid file-hint anchor-hint))))

(define (wikilink-repair-url name)
  (string-append "tmfs://wikilink-repair/" name))

(tmfs-load-handler (wikilink-repair name)
  (wikilink-repair-handler-sub name))

(tmfs-load-handler (ns name)
  (tree->stree (namespace-info-page name)))

(tmfs-load-handler (welcome name)
  (tree->stree (vault-welcome-page)))

(tmfs-load-handler (artifact name)
  (if (artifact-open-uuid name)
      `(document
         (style (tuple "generic"))
         (body (document "Opening artifact...")))
      `(document
         (style (tuple "generic"))
         (body (document (bold "Artifact not found: ") ,name)))))

(tmfs-title-handler (artifact name doc)
  "Artifact")

(tmfs-permission-handler (artifact name kind)
  (== kind "read"))

;; Native dispatch handles node-model URLs; retain the map-mode repair path.
(tm-define (go-to-url u . opt-from)
  (:require (and (vault-wikilink-navigation-url? u) (not (vault-node-model-active?))))
  (when (pair? opt-from) (cursor-history-add (car opt-from)))
  (let ((name (string-copy (vault-wikilink-navigation-name u))))
    (exec-global
      (lambda ()
        (let ((target (wikilink-navigation-target name)))
          (if target
              (apply vault-jump-to-source target)
              (load-browse-buffer
                (system->url (wikilink-repair-url name)))))))))

(tmfs-load-handler (artifact-disambiguation name)
  (tree->stree (artifact-disambiguation-page name)))

(tmfs-title-handler (artifact-disambiguation name doc)
  "Artifact disambiguation")

(tmfs-permission-handler (artifact-disambiguation name kind)
  (== kind "read"))

(tmfs-load-handler (artifact-name-query name)
  (tree->stree (artifact-name-query-page name)))

(tmfs-title-handler (artifact-name-query name doc)
  "Artifact disambiguation")

(tmfs-permission-handler (artifact-name-query name kind)
  (== kind "read"))

(define (wikilink-trigger-repair uuid file-hint anchor-hint)
  (display* "Trigger repair for " uuid ", hint: " file-hint "\n")
  (if (string-null? file-hint)
      `(document (style (tuple "generic")) (body (document (bold "Error: ") "Broken Wikilink and no file hint provided.")))
      (begin
        (system-wait "Searching vault" (string-append "for " file-hint))
        (let* ((limit (get-fuzzy-limit))
               (files (vault-scan-files (vault-get-root) file-hint))
               (candidates '()))
          (display* "  Candidates found: " (length files) "\n")
          (for (f (sublist files 0 (min (length files) limit)))
            (let* ((rel (url->unix (url-delta (url-append (vault-get-root) "") f)))
                   (anchor (vault-find-anchor-in-file f anchor-hint)))
              (display* "    Candidate relative path: " rel "\n")
              (set! candidates (cons (list rel anchor) candidates))))
          
          (system-wait "" "") ;; Close progress
          (if (null? candidates)
              (begin
                (display* "  No candidates, returning error page\n")
                `(document (style (tuple "generic")) (body (document (bold "Error: ") "Could not find any matches for: " ,file-hint))))
              (begin
                (display* "  Returning repair page with " (length candidates) " items\n")
                (wikilink-repair-page uuid file-hint anchor-hint (reverse candidates))))))))

(define (wikilink-repair-page uuid f-hint a-hint candidates)
  `(document
     (style (tuple "generic"))
     (body (document
       (section "Repair Wikilink")
       "The link target could not be resolved. Select a candidate to repair:"
       (enumerate
         (document
           ,@(map (lambda (c)
                    (let* ((path (car c))
                           (anchor (cadr c))
                           (existing (vault-find-uuid path "" anchor))
                           (new-uuid (if (and (string? existing) (not (string-null? existing)))
                                         existing
                                         (vault-generate-uuid)))
                           (label (string-append path " #" anchor))
                           (target (vault-wikilink-repair-choice-url
                                     uuid new-uuid path anchor)))
                      `(concat (item)
                               (hlink ,label ,target)
                               " (UUID: " ,new-uuid ")")))
                  candidates)))))))

;; Native callbacks retain the kernel preference registry's initialization order.
(register-preference-callback-procedures
  (list notify-cursor-color notify-document-background-color notify-enunciation-color notify-focus-border-width notify-focus-color notify-labels-mode notify-link-color notify-selection-color notify-vault-explorer-track notify-vault-preferences-mode))
