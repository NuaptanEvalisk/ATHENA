(texmacs-module (texmacs texmacs tm-vault)
  (:use (kernel boot abbrevs)
        (kernel library list)
        (kernel library tree)
        (kernel texmacs tm-define)
        (kernel texmacs tm-file-system)
        (kernel texmacs tm-secure)))

(define-secure-symbols wikilink-repair-apply)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Settings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-preferences
  ("vault fuzzy search limit" "3" noop))

(define (get-fuzzy-limit)
  (let ((pref (get-preference "vault fuzzy search limit")))
    (or (string->number pref) 3)))

(tm-widget (vault-preferences-widget)
  (aligned
    (item (text "Fuzzy search limit:")
      (enum (set-preference "vault fuzzy search limit" answer)
            '("1" "2" "3" "5" "10")
            (get-preference "vault fuzzy search limit")
            "10em"))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vault management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (interactive-new-vault dir)
  (interactive (lambda (name)
                 (let* ((vault-file (url-append dir "Vaultfile"))
                        (db-path "map.tmdb"))
                   (save-object vault-file (list name db-path))
                   (vault-load dir name db-path)
                   (set-message (string-append "Created vault: " name) "Vault")))
               '("Vault name" "string")))

(tm-define (load-vault-dir dir)
  (let* ((vault-file (url-append dir "Vaultfile")))
    (if (url-exists? vault-file)
        (let ((data (load-object vault-file)))
          (if (and (list? data) (>= (length data) 2))
              (begin
                (vault-load dir (car data) (cadr data))
                (set-message (string-append "Loaded vault: " (car data)) "Vault"))
              (set-message "Invalid Vaultfile" "Error")))
        (interactive-new-vault dir))))

(tm-define (open-vault)
  (choose-file load-vault-dir "Load Vault" "directory"))

(tm-define (insert-wikilink)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault. Please load a vault first." "Error")
      (let ((res (vault-choose-link)))
        (if (and (tree? res) (== (tree-label res) 'tuple))
            (let* ((rel-path (tree->string (tree-ref res 0)))
                   (anchor (tree->string (tree-ref res 1)))
                   (file-hint (tree->string (tree-ref res 2)))
                   (anchor-hint (tree->string (tree-ref res 3)))
                   (display-text (tree->string (tree-ref res 4)))
                   (uuid (vault-find-uuid rel-path "" anchor)))
              (if (string-null? uuid)
                  (begin
                    (set! uuid (vault-generate-uuid))
                    (vault-set-node uuid rel-path "" anchor)))
              (insert `(hlink ,display-text 
                              ,(string-append "tmfs://wikilink/" uuid "/" 
                                              file-hint "/" anchor-hint))))))))

(kbd-commands
  ("=" "Insert Wikilink" (if (in-text?) (insert-wikilink))))


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
        (let* ((all (url-read-directory dir "*.tm"))
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

(tm-define (wikilink-repair-apply bad-uuid new-uuid path anchor-begin anchor-end)
  (display* "Applying repair: " bad-uuid " -> " new-uuid " at " path "\n")
  (vault-set-node new-uuid path anchor-begin anchor-end)
  (let* ((old-link (string-append "tmfs://wikilink/" bad-uuid))
         (new-link (string-append "tmfs://wikilink/" new-uuid))
         (old-link-alt (string-append "tmfs://Wikilink/" bad-uuid))
         (bufs (buffer-list)))
    (for (b bufs)
      (let* ((t (buffer-get b)))
        (tree-search t (lambda (node)
                         (if (and (tree-compound? node)
                                  (== (tree-label node) 'hlink)
                                  (>= (tree-arity node) 2))
                             (let ((url (tree->string (tree-ref node 1))))
                               (if (or (string-starts? url old-link)
                                       (string-starts? url old-link-alt))
                                   (let* ((base (if (string-starts? url old-link) old-link old-link-alt))
                                          (suffix (string-drop url (string-length base))))
                                     (tree-child-set! node 1 (string-append new-link suffix))))))
                         #f))))
    (set-message "Link repaired" "Vault")
    (load-buffer (url-append (vault-get-root) (unix->url path)))
    (if (not (string-null? anchor-end))
        (delayed (:idle 100) (go-to-label anchor-end)))))

(define (wikilink-handler-sub name)
  (display* "Wikilink load: " name "\n")
  (let* ((parts (string-tokenize-by-char name #\/))
         (uuid (if (pair? parts) (car parts) ""))
         (file-hint (if (and (pair? parts) (pair? (cdr parts))) (cadr parts) ""))
         (anchor-hint (if (and (pair? parts) (pair? (cdr parts)) (pair? (cddr parts))) (caddr parts) ""))
         (node (vault-get-node uuid)))
    (display* "  UUID: " uuid ", hints: " file-hint ", " anchor-hint "\n")
    (display* "  Node: " node "\n")
    
    (if (and (tree? node) (== (tree-label node) 'tuple))
        (let* ((rel-path (tree->string (tree-ref node 0)))
               (a-begin (tree->string (tree-ref node 1)))
               (a-end (tree->string (tree-ref node 2)))
               (abs-url (url-append (vault-get-root) (unix->url rel-path))))
          (display* "  Target: " abs-url "\n")
          (if (url-exists? abs-url)
              (begin
                (display* "  Opening target via delayed execution...\n")
                (exec-delayed (lambda ()
                                (display* "  Executing load-buffer for " abs-url "\n")
                                (load-buffer abs-url)
                                (if (not (string-null? a-end))
                                    (begin
                                      (display* "  Jumping to label " a-end "\n")
                                      (delayed (:idle 100) (go-to-label a-end))))
                                (display* "  Navigation complete.\n")))
                `(document (TeXmacs ,(texmacs-version)) 
                           (style (tuple "generic")) 
                           (body (document "Redirecting..."))))
              (begin
                (display* "  Target file missing on disk, triggering repair...\n")
                (wikilink-trigger-repair uuid file-hint anchor-hint))))
        (begin
          (display* "  UUID not found in database, triggering repair...\n")
          (wikilink-trigger-repair uuid file-hint anchor-hint)))))

(tmfs-load-handler (wikilink name)
  (wikilink-handler-sub name))

(tmfs-load-handler (Wikilink name)
  (wikilink-handler-sub name))

(define (wikilink-trigger-repair uuid file-hint anchor-hint)
  (display* "Trigger repair for " uuid ", hint: " file-hint "\n")
  (if (string-null? file-hint)
      `(document (TeXmacs ,(texmacs-version)) (style (tuple "generic")) (body (document (bold "Error: ") "Broken Wikilink and no file hint provided.")))
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
                `(document (TeXmacs ,(texmacs-version)) (style (tuple "generic")) (body (document (bold "Error: ") "Could not find any matches for: " ,file-hint))))
              (begin
                (display* "  Returning repair page with " (length candidates) " items\n")
                (wikilink-repair-page uuid file-hint anchor-hint (reverse candidates))))))))

(define (wikilink-repair-page uuid f-hint a-hint candidates)
  `(document
     (TeXmacs ,(texmacs-version))
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
                           (cmd (string-append "(wikilink-repair-apply " 
                                               (object->string uuid) " "
                                               (object->string new-uuid) " "
                                               (object->string path) " \"\" "
                                               (object->string anchor) ")")))
                      `(concat (item)
                               (action ,label ,cmd)
                               " (UUID: " ,new-uuid ")")))
                  candidates)))))))
