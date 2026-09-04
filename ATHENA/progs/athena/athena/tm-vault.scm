(texmacs-module (athena athena tm-vault)
  (:use (kernel boot abbrevs)
        (kernel library list)
        (kernel library tree)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)
        (utils library cursor)
        (generic document-edit)
        (fonts font-new-widgets)
        (link link-navigate)
        (athena athena tm-vault-bugcheck)
        (athena athena tm-vault-images)
        (athena athena tm-vault-anchors)
        (athena athena tm-vault-maintenance)
        (athena athena tm-vault-namespaces)
        (athena athena tm-vault-quick-switcher)
        (athena athena tm-vault-recents)
        (athena athena tm-vault-startup)
        (athena athena tm-vault-welcome)
        (athena menus file-menu)))
(import-from (kernel athena tm-preferences))


(tm-define (vault-jump-to-source path anchor)
  (load-buffer path)
  (if (!= anchor "")
      (delayed (:idle 100) (go-to-label anchor))))

(tm-define (artifact-jump-to-position path position)
  (load-buffer path)
  (and-with target
      (path->tree (append (tree->path (buffer-tree)) position))
    (tree-go-to target :start)
    (cursor-show-if-hidden)
    ;; Expanding a folded ancestor moves the cursor to that variant's entry.
    ;; Re-resolve the tree handle, then restore the exact artifact position.
    (and-with visible-target
        (path->tree (append (tree->path (buffer-tree)) position))
      (tree-go-to visible-target :start))))

(tm-define (artifact-navigation-failed path)
  (set-message
    (string-append "Artifact definition changed in " path
                   "; rebuild artifacts for this document")
    "Artifact"))

(tm-define (ext-get-preference key def)
  (let ((val (get-preference key)))
    (if (string-null? val) def val)))

(define-secure-symbols wikilink-repair-apply vault-transclude-repair
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
                       ext-get-preference
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

(define (vault-wikilink-url uuid file-hint anchor-hint)
  (string-append "tmfs://wikilink/"
                 (vault-url-component-encode uuid) "/"
                 (vault-url-component-encode file-hint) "/"
                 (vault-url-component-encode anchor-hint)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Settings
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (notify-cursor-color name val)
  (gui-set-cursor-color val))

(define (notify-selection-color name val)
  (gui-set-selection-color val))

(define (notify-focus-color name val)
  (gui-set-focus-color val))

(define (notify-focus-border-width name val)
  (gui-set-focus-border-width val))

(define (notify-link-color name val)
  (refresh-now ""))

(define (notify-document-background-color name val)
  (refresh-now ""))

(define (notify-labels-mode name val)
  (refresh-now "labels"))

(define (notify-enunciation-color name val)
  (refresh-now "enunciations"))

(define (notify-vault-explorer-track name val)
  (if (== val "on") (vault-track-current-buffer-if-enabled)))

(define vault-preferences-active? #f)
(define vault-preferences-file #f)
(define vault-preferences-reloading? #f)

(define (vault-system-preferences-file)
  "$ATHENA_HOME_PATH/system/preferences.json")

(define (vault-take-preferences?)
  (== (get-preference "vault take preferences with vault") "on"))

(define (notify-vault-preferences-mode name val)
  (when (not vault-preferences-reloading?)
    (cond ((and (== val "off") vault-preferences-active?)
           (vault-preferences-deactivate))
          ((and (== val "on") (vault-active?) (not vault-preferences-active?))
           (vault-preferences-activate-current)))))

(define (vaultfile-preferences-path data)
  (if (and (list? data) (>= (length data) 3) (string? (caddr data)))
      (caddr data)
      ""))

(define (vaultfile-namespace-db-path data)
  (if (and (list? data) (>= (length data) 4) (string? (cadddr data)))
      (cadddr data)
      "ns.sqlite"))

(define (vaultfile-startup-page data)
  (if (and (list? data) (>= (length data) 5) (string? (list-ref data 4)))
      (list-ref data 4)
      ""))

(define (vaultfile-one-time-startup-page data)
  (if (and (list? data) (>= (length data) 6) (string? (list-ref data 5)))
      (list-ref data 5)
      ""))

(define (vaultfile-maintenance-summary-path data)
  (if (and (list? data) (>= (length data) 7) (string? (list-ref data 6)))
      (list-ref data 6)
      ""))

(define (vaultfile-rag-index-path data)
  (if (and (list? data) (>= (length data) 8) (string? (list-ref data 7))
           (not (string-null? (list-ref data 7))))
      (list-ref data 7)
      "rag.sqlite"))

(define (vaultfile-websites-path data)
  (if (and (list? data) (>= (length data) 9) (string? (list-ref data 8))
           (not (string-null? (list-ref data 8))))
      (list-ref data 8)
      "websites.json"))

(define (vaultfile-root-namespace data)
  (if (and (list? data) (>= (length data) 10) (string? (list-ref data 9)))
      (list-ref data 9)
      ""))

(define (vaultfile-artifacts-path data)
  (if (and (list? data) (>= (length data) 11) (string? (list-ref data 10))
           (not (string-null? (list-ref data 10))))
      (list-ref data 10)
      "artifacts.db"))

(define (vaultfile-enunciations-path data)
  (if (and (list? data) (>= (length data) 12) (string? (list-ref data 11))
           (not (string-null? (list-ref data 11))))
      (list-ref data 11)
      "enunciations.db"))

(define (vaultfile-bold-text-path data)
  (if (and (list? data) (>= (length data) 13) (string? (list-ref data 12))
           (not (string-null? (list-ref data 12))))
      (list-ref data 12)
      "bold-text.db"))

(define (vaultfile-materials-db-path data)
  (if (and (list? data) (>= (length data) 14) (string? (list-ref data 13))
           (not (string-null? (list-ref data 13))))
      (list-ref data 13)
      "materials.sqlite"))

(define (vaultfile-materials-directory data)
  (if (and (list? data) (>= (length data) 15) (string? (list-ref data 14))
           (not (string-null? (list-ref data 14))))
      (list-ref data 14)
      "materials"))

(define (vaultfile-artifact-title-filter-path data)
  (if (and (list? data) (>= (length data) 16) (string? (list-ref data 15))
           (not (string-null? (list-ref data 15))))
      (list-ref data 15)
      "artifact-title-filter.lst"))

(define (vaultfile-normalized data)
  (list (car data)
        (cadr data)
        (vaultfile-preferences-path data)
        (vaultfile-namespace-db-path data)
        (vaultfile-startup-page data)
        (vaultfile-one-time-startup-page data)
        (vaultfile-maintenance-summary-path data)
        (vaultfile-rag-index-path data)
        (vaultfile-websites-path data)
        (vaultfile-root-namespace data)
        (vaultfile-artifacts-path data)
        (vaultfile-enunciations-path data)
        (vaultfile-bold-text-path data)
        (vaultfile-materials-db-path data)
        (vaultfile-materials-directory data)
        (vaultfile-artifact-title-filter-path data)))

(define (vaultfile-write! dir data)
  (let ((err (vaultfile-write dir (vaultfile-normalized data))))
    (when (and (string? err) (!= err ""))
      (show-message err "Vaultfile"))
    (or (not (string? err)) (== err ""))))

(define (vaultfile-normalize! dir data)
  (let ((normalized (vaultfile-normalized data)))
    (when (not (equal? data normalized))
      (vaultfile-write! dir normalized))
    normalized))

(define (vaultfile-with-preferences data prefs-path)
  (list (car data)
        (cadr data)
        prefs-path
        (vaultfile-namespace-db-path data)
        (vaultfile-startup-page data)
        (vaultfile-one-time-startup-page data)
        (vaultfile-maintenance-summary-path data)
        (vaultfile-rag-index-path data)
        (vaultfile-websites-path data)
        (vaultfile-root-namespace data)
        (vaultfile-artifacts-path data)
        (vaultfile-enunciations-path data)
        (vaultfile-bold-text-path data)
        (vaultfile-materials-db-path data)
        (vaultfile-materials-directory data)
        (vaultfile-artifact-title-filter-path data)))

(define (vault-preferences-url dir prefs-path)
  (url-append dir prefs-path))

(define (vault-preferences-json-path prefs-path)
  (cond ((string-null? prefs-path) "vprefs.json")
        ((string-ends? prefs-path ".json") prefs-path)
        ((string-ends? prefs-path ".scm")
         (string-append
          (substring prefs-path 0 (- (string-length prefs-path) 4))
          ".json"))
        (else (string-append prefs-path ".json"))))

(define (vault-preferences-ensure-file dir data)
  (let* ((rel (vaultfile-preferences-path data))
         (prefs-rel (vault-preferences-json-path rel))
         (prefs-file (vault-preferences-url dir prefs-rel))
         (legacy-file (and (!= rel prefs-rel)
                           (vault-preferences-url
                            dir (if (string-null? rel) "vprefs.scm" rel)))))
    (when (!= rel prefs-rel)
      (vaultfile-write! dir (vaultfile-with-preferences data prefs-rel)))
    (when (not (url-exists? prefs-file))
      (if (and legacy-file (url-exists? legacy-file))
          (cpp-load-preferences legacy-file)
          (cpp-dump-preferences prefs-file)))
    prefs-file))

(define (vault-preferences-activate dir data)
  (let ((prefs-file (vault-preferences-ensure-file dir data)))
    (set! vault-preferences-file prefs-file)
    (set! vault-preferences-active? #t)
    (set! vault-preferences-reloading? #t)
    (load-preferences-from prefs-file)
    (set! vault-preferences-reloading? #f)))

(define (vault-preferences-activate-current)
  (if (vault-active?)
      (let* ((dir (vault-get-root))
             (data (if (vaultfile-present? dir) (vaultfile-read dir) '())))
        (if (and (list? data) (>= (length data) 2))
            (vault-preferences-activate dir (vaultfile-normalize! dir data))))))

(define (vault-preferences-deactivate)
  (when vault-preferences-active?
    (save-preferences)
    (set! vault-preferences-active? #f)
    (set! vault-preferences-file #f)
    (set! vault-preferences-reloading? #t)
    (load-preferences-from (vault-system-preferences-file))
    (set! vault-preferences-reloading? #f)))


(define (get-fuzzy-limit)
  (let ((pref (get-preference "vault fuzzy search limit")))
    (or (string->number pref) 3)))

(define (vault-font-preference-choices)
  (list-remove-duplicates
    (cons-new (get-preference "vault preferred font")
      (append '("" "roman" "stix" "bonum" "pagella" "schola" "termes")
              (get-user-preferred-fonts)
              (font-database-families)))))

(tm-define (vault-apply-preferred-font-to-current-buffer)
  (when (and (defined? 'vault-active?)
             (defined? 'init-font)
             (vault-active?))
    (let ((font (get-preference "vault preferred font")))
      (when (!= font "")
        (init-font font)))))

(tm-widget (vault-preferences-widget)
  (vertical
    (aligned
      (item (text "Popup fuzzy search limit:")
        (enum (set-preference "vault fuzzy search limit" answer)
              '("1" "2" "3" "5" "10")
              (get-preference "vault fuzzy search limit")
              "10em"))
      (item (text "Auto load last vault:")
        (toggle (set-preference "vault auto load last" (if answer "on" "off"))
                (equal? (get-preference "vault auto load last") "on")))
      (item (text "Report if last vault is unavailable:")
        (toggle (set-preference "vault report missing last" (if answer "on" "off"))
                (equal? (get-preference "vault report missing last") "on")))
      (item (text "Show vault welcome page on start:")
        (toggle (set-preference "vault welcome page" (if answer "on" "off"))
                (equal? (get-preference "vault welcome page") "on")))
      (item (text "Show vault explorer on startup:")
        (toggle (set-preference "vault explorer show on startup" (if answer "on" "off"))
                (equal? (get-preference "vault explorer show on startup") "on")))
      (item (text "Take preferences with vault:")
        (toggle (set-preference "vault take preferences with vault" (if answer "on" "off"))
                (equal? (get-preference "vault take preferences with vault") "on")))
      (item (text "Track current file in vault explorer:")
        (toggle (set-preference "vault explorer track current file" (if answer "on" "off"))
                (equal? (get-preference "vault explorer track current file") "on")))
      (item (text "Use system trash for safe deletion:")
        (toggle (set-preference "vault explorer use system trash" (if answer "on" "off"))
                (equal? (get-preference "vault explorer use system trash") "on")))
      (item (text "Namespace explorer shows file matches only for leaf namespaces:")
        (toggle (set-preference "vault namespace explorer leaf matches only" (if answer "on" "off"))
                (equal? (get-preference "vault namespace explorer leaf matches only") "on")))
      (item (text "Simplify hierarchy graphs:")
        (toggle (set-preference "vault simplify hierarchy graphs" (if answer "on" "off"))
                (equal? (get-preference "vault simplify hierarchy graphs") "on")))
      (item (text "Max allowed number of full backups:")
        (enum (set-preference "vault max full backups" answer)
              '("Unlimited" "1" "2" "3" "5" "10" "20" "50")
              (get-preference "vault max full backups")
              "10em"))
      (item (text "Preservation of pre-save histories for file:")
        (enum (set-preference "vault pre-save history preservation" answer)
              '("Unlimited" "1 hour" "6 hours" "1 day" "3 days" "1 week" "1 month")
              (get-preference "vault pre-save history preservation")
              "10em"))
      (item (text "Collect orphan assets during vault maintenance:")
        (toggle (set-preference "vault collect orphan assets" (if answer "on" "off"))
                (equal? (get-preference "vault collect orphan assets") "on")))
      (item (text "Consume %s aggressively in sub-product naming template suggestion:")
        (toggle (set-preference "vault subproduct consume string aggressively" (if answer "on" "off"))
                (equal? (get-preference "vault subproduct consume string aggressively") "on")))
      (item (text "Global preferred font for vault:")
        (enum (set-preference "vault preferred font" answer)
              (vault-font-preference-choices)
              (get-preference "vault preferred font")
              "18em")))
    (dynamic (vault-anchor-preferences-widget))
    (dynamic (vault-image-preferences-widget))))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vault management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (vault-load/namespace-db dir name db-path ns-path)
  (if (defined? 'vault-load-with-ns)
      (vault-load-with-ns dir name db-path ns-path)
      (vault-load dir name db-path)))

(define (vault-report-root-namespace-error)
  (when (defined? 'vault-validate-root-namespace)
    (let ((err (vault-validate-root-namespace)))
      (when (and (string? err) (!= err ""))
        (show-message err "Vault")))))

(tm-define (interactive-new-vault dir)
  (interactive (lambda (name)
                 (let* ((db-path "map.sqlite")
                        (ns-path "ns.sqlite")
                        (data (list name db-path "" ns-path "" "" ""
                                    "rag.sqlite" "websites.json" "")))
                   (if (vaultfile-write! dir data)
                       (let ((err (vault-load/namespace-db
                                   dir name db-path ns-path)))
                         (if (and (string? err) (!= err ""))
                             (show-message err "Vault")
                             (begin
                               (if (vault-take-preferences?)
                                   (vault-preferences-activate dir data))
                               (set-message
                                (string-append "Created vault: " name)
                                "Vault")
                               (vault-report-root-namespace-error)))))))
               '("Vault name" "string")))

(define (load-vault-dir-main dir)
  (if (vaultfile-present? dir)
      (let ((err (vaultfile-ensure-json dir)))
        (if (and (string? err) (!= err ""))
            (show-message err "Vault")
            (let ((data (vaultfile-read dir)))
              (if (and (list? data) (>= (length data) 2))
                  (let* ((data (vaultfile-normalize! dir data))
                         (take-prefs? (vault-take-preferences?)))
                    (when vault-preferences-active? (save-preferences))
                    (let ((err (vault-load/namespace-db
                                dir (car data) (cadr data)
                                (vaultfile-namespace-db-path data))))
                      (if (and (string? err) (!= err ""))
                          (show-message err "Vault")
                          (let ((loaded-data (vaultfile-read dir)))
                            (vault-preferences-deactivate)
                            (if take-prefs?
                                (vault-preferences-activate
                                 dir loaded-data))
                            (add-recent-vault dir)
                            (set-message
                             (string-append "Loaded vault: "
                                            (car loaded-data))
                             "Vault")
                            (vault-report-root-namespace-error)))))
                  (set-message "Invalid Vaultfile.json" "Error")))))
      (interactive-new-vault dir)))

(tm-define (load-vault-dir dir)
  (exec-global (lambda () (load-vault-dir-main dir))))

(tm-define (open-vault)
  (choose-file load-vault-dir "Load Vault" "directory"))

(tm-define (unload-vault)
  (:interactive #t)
  (exec-global
    (lambda ()
      (if (vault-active?)
          (begin
            (vault-preferences-deactivate)
            (vault-close)
            (set-message "Unloaded vault" "Vault"))
          (set-message "No active vault to unload" "Vault")))))

(tm-define (open-vault-explorer)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Vault Explorer")
      (vault-show-explorer-and-track)))

(tm-define (open-vault-backup-viewer)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first."
                    "Vault Backup Viewer")
      (vault-backup-viewer-show)))

(tm-define (insert-wikilink)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault. Please load a vault first." "Error")
      (let ((res (vault-choose-link #f)))
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
                              ,(vault-wikilink-url uuid file-hint anchor-hint))))))))

(tm-define (insert-transclude)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault. Please load a vault first." "Error")
      (let ((res (vault-choose-link #t)))
        (if (and (tree? res) (== (tree-label res) 'tuple))
            (let* ((rel-path (tree->string (tree-ref res 0)))
                   (anchor-b (tree->string (tree-ref res 1)))
                   (anchor-e (tree->string (tree-ref res 2)))
                   (file-hint (tree->string (tree-ref res 3)))
                   (anchor-hint (tree->string (tree-ref res 4)))
                   (uuid (vault-find-uuid rel-path anchor-b anchor-e)))
              (if (string-null? uuid)
                  (begin
                    (set! uuid (vault-generate-uuid))
                    (vault-set-node uuid rel-path anchor-b anchor-e)))
              (insert `(transclude ,uuid ,file-hint ,anchor-b ,anchor-e)))))))

(define (vault-transclude-replace-in-buffer! buf uuid file-hint
                                             anchor-b anchor-e)
  (let ((changed? #f)
        (t (buffer-get buf)))
    (tree-search t
      (lambda (node)
        (when (and (tree-compound? node)
                   (== (tree-label node) 'transclude)
                   (>= (tree-arity node) 1)
                   (== (tree->string (tree-ref node 0)) uuid))
          (tree-set! node `(transclude ,uuid ,file-hint
                                       ,anchor-b ,anchor-e))
          (set! changed? #t))
        #f))
    changed?))

(tm-define (vault-transclude-repair bad-uuid old-file-hint old-anchor-b
                                    old-anchor-e)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault. Please load a vault first." "Error")
      (let ((res (vault-choose-link #t)))
        (if (and (tree? res) (== (tree-label res) 'tuple))
            (let* ((rel-path (tree->string (tree-ref res 0)))
                   (anchor-b (tree->string (tree-ref res 1)))
                   (anchor-e (tree->string (tree-ref res 2)))
                   (file-hint (tree->string (tree-ref res 3))))
              (vault-set-node bad-uuid rel-path anchor-b anchor-e)
              (let ((changed 0))
                (for (b (buffer-list))
                  (when (vault-transclude-replace-in-buffer!
                         b bad-uuid file-hint anchor-b anchor-e)
                    (set! changed (+ changed 1))))
                (if (> changed 0)
                    (set-message "Transclusion repaired" "Vault")
                    (set-message
                     "Transclusion target repaired, but no open source node matched"
                     "Vault"))))))))

(kbd-commands
  ("=" "Insert Wikilink" (if (in-text?) (insert-wikilink)))
  ("+" "Insert Transclusion" (if (in-text?) (insert-transclude))))

(define (vault-focused-transclusion)
  (let ((focused (focus-tree)))
    (if (and (tree? focused) (tree-is? focused 'transclude)) focused
        (tree-innermost 'transclude #t))))

(define (vault-go-outside-tree t forwards?)
  (and-let* ((parent (tree-up t))
             (index (tree-index t)))
    (cond ((and forwards? (< (+ index 1) (tree-arity parent)))
           (tree-go-to parent (+ index 1) :start))
          ((and (not forwards?) (> index 0))
           (tree-go-to parent (- index 1) :end))
          (else (vault-go-outside-tree parent forwards?)))))

(define (vault-go-before-transclusion)
  (and-with t (vault-focused-transclusion)
    (selection-cancel)
    (vault-go-outside-tree t #f)))

(define (vault-go-after-transclusion)
  (and-with t (vault-focused-transclusion)
    (selection-cancel)
    (vault-go-outside-tree t #t)))

(define (vault-select-transclusion)
  (and-with t (vault-focused-transclusion)
    (tree-select t)))

(tm-menu (vault-transclusion-focus-menu)
  ("Move before transclusion" (vault-go-before-transclusion))
  ("Select transclusion" (vault-select-transclusion))
  ("Move after transclusion" (vault-go-after-transclusion)))


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

(define active-transclusions '())

(define (vault-strip-labels st)
  (cond ((not (pair? st)) st)
        ((eq? (car st) 'label) '(concat))
        (else (cons (car st) (map vault-strip-labels (cdr st))))))

(define (vault-absolute-image-path? path)
  (or (string-null? path)
      (string-starts? path "/")
      (string-starts? path "~")
      (string-starts? path "$")
      (string-occurs? "://" path)))

(define (vault-transclude-rebase-image-path path source-dir)
  (if (vault-absolute-image-path? path) path
      (let* ((decoded (cork->utf8 path))
             (absolute (url-append source-dir (unix->url decoded))))
        (utf8->cork (url->system absolute)))))

(define (vault-transclude-rebase-images st source-dir)
  (cond ((not (pair? st)) st)
        ((and (eq? (car st) 'image)
              (pair? (cdr st))
              (string? (cadr st)))
         (cons 'image
               (cons (vault-transclude-rebase-image-path (cadr st) source-dir)
                     (map (cut vault-transclude-rebase-images <> source-dir)
                          (cddr st)))))
        (else (cons (car st)
                    (map (cut vault-transclude-rebase-images <> source-dir)
                         (cdr st))))))

(tm-define (vault-resolve-transclude-content uuid f-hint b-hint e-hint)
  (let* ((uuid-str (tree->string uuid))
         (f-hint-str (tree->string f-hint))
         (b-hint-str (tree->string b-hint))
         (e-hint-str (tree->string e-hint)))
    (if (member uuid-str active-transclusions)
        `(document (with "color" "red"
                    (concat (bold "Broken Transclusion: ")
                            "Cyclic transclusion detected (" ,f-hint-str ").")))
        (let ((res #f))
          (catch #t
            (lambda ()
              (set! active-transclusions (cons uuid-str active-transclusions))
              (let* ((node (vault-get-node uuid-str)))
                (set! res
                      (if (and (tree? node) (== (tree-label node) 'tuple))
                          (let* ((rel-path (tree->string (tree-ref node 0)))
                                 (a-begin (tree->string (tree-ref node 1)))
                                 (a-end (tree->string (tree-ref node 2)))
                                 (abs-url (url-append (vault-get-root)
                                                      (unix->url rel-path)))
                                 (source-dir (url-head abs-url)))
                            (if (url-exists? abs-url)
                                (let* ((t (tree-import abs-url "texmacs"))
                                       (content (vault-extract-range
                                                 t a-begin a-end)))
                                  (if (null? content)
                                      `(document ,(vault-transclude-error
                                                   uuid-str f-hint-str
                                                   b-hint-str e-hint-str
                                                   "Anchors not found in target."))
                                      `(document
                                         ,@(map
                                            (lambda (st)
                                              (vault-transclude-rebase-images
                                               (vault-strip-labels st)
                                               source-dir))
                                            (map tree->stree content)))))
                                `(document ,(vault-transclude-error
                                             uuid-str f-hint-str
                                             b-hint-str e-hint-str
                                             "Target file missing."))))
                          `(document ,(vault-transclude-error
                                       uuid-str f-hint-str
                                       b-hint-str e-hint-str
                                       "UUID not in database."))))))
            (lambda (key . args)
              (display* "Transclude export error: " key " " args "\n")
              (set! res
                    `(document ,(vault-transclude-error
                                 uuid-str f-hint-str b-hint-str e-hint-str
                                 "Internal error.")))))
          (set! active-transclusions
                (list-difference active-transclusions (list uuid-str)))
          res))))

(define (vault-flatten-cycle-error file-hint)
  `(with "color" "red"
     (concat (bold "Broken Transclusion: ")
             "Cyclic transclusion detected (" ,file-hint ").")))

(define (vault-flatten-stree* st resolver active)
  (cond
    ((not (pair? st)) st)
    ((and (eq? (car st) 'transclude) (>= (length st) 5))
     (let ((uuid (cadr st))
           (file-hint (caddr st)))
       (if (member uuid active)
           (vault-flatten-cycle-error file-hint)
           (let ((resolved (resolver (cadr st) (caddr st)
                                     (cadddr st) (list-ref st 4))))
             (vault-flatten-stree*
               (if (tree? resolved) (tree->stree resolved) resolved)
               resolver (cons uuid active))))))
    ((eq? (car st) 'document)
     (cons 'document
           (append-map
             (lambda (child)
               (let ((flat (vault-flatten-stree* child resolver active)))
                 (if (and (pair? flat) (eq? (car flat) 'document))
                     (cdr flat)
                     (list flat))))
             (cdr st))))
    (else
      (cons (car st)
            (map (lambda (child)
                   (vault-flatten-stree* child resolver active))
                 (cdr st))))))

(define (vault-flatten-stree st resolver)
  (vault-flatten-stree* st resolver '()))

(tm-define (vault-flatten-document)
  (:interactive #t)
  (if (not (vault-active?))
      (set-message "No active vault. Please load a vault first." "Flatten")
      (let* ((source (current-buffer))
             (source-title (buffer-get-title source))
             (target (buffer-new)))
        (buffer-copy source target)
        (let* ((count 0)
               (resolver (lambda (uuid file-hint anchor-b anchor-e)
                           (set! count (+ count 1))
                           (vault-resolve-transclude-content
                             uuid file-hint anchor-b anchor-e)))
               (body (tree->stree (buffer-get-body target)))
               (flattened (vault-flatten-stree
                            body resolver)))
          (buffer-set-body target (stree->tree flattened))
          (buffer-set-title target
            (string-append
              (if (string-null? source-title) "Untitled" source-title)
              " (flattened)"))
          (switch-to-buffer target)
          (set-message
            (string-append "Flattened " (number->string count)
                           (if (= count 1) " transclusion" " transclusions")
                           " into a new document")
            "Flatten")))))

(tm-define (vault-resolve-transclude uuid f-hint b-hint e-hint)
  (let* ((uuid-str (tree->string uuid))
         (f-hint-str (tree->string f-hint))
         (b-hint-str (tree->string b-hint))
         (e-hint-str (tree->string e-hint)))
    (if (member uuid-str active-transclusions)
        `(with "color" "red" (concat (bold "Broken Transclusion: ") "Cyclic transclusion detected (" ,f-hint-str ")."))
        (let ((res #f))
          (catch #t
            (lambda ()
              (set! active-transclusions (cons uuid-str active-transclusions))
              (let* ((node (vault-get-node uuid-str)))
                (set! res 
                      (if (and (tree? node) (== (tree-label node) 'tuple))
                          (let* ((rel-path (tree->string (tree-ref node 0)))
                                 (a-begin (tree->string (tree-ref node 1)))
                                 (a-end (tree->string (tree-ref node 2)))
                                 (abs-url (url-append (vault-get-root) (unix->url rel-path)))
                                 (source-dir (url-head abs-url))
                                 (filename (url->system (url-tail abs-url))))
                            (if (url-exists? abs-url)
                                (let* ((t (tree-import abs-url "texmacs"))
                                       (content (vault-extract-range t a-begin a-end)))
                                  (if (null? content)
                                      (vault-transclude-error uuid-str f-hint-str b-hint-str e-hint-str "Anchors not found in target.")
                                      (let* ((btn-cmd (string-append "(vault-jump-to-source " 
                                                                     (object->string (url->string abs-url)) " "
                                                                     (object->string a-begin) ")"))
                                             (bg-color (get-preference "vault transclusion color")))
                                        `(with "ornament-color" ,bg-color
                                               "ornament-shape" "rectangular"
                                               "ornament-border" "1ln"
                                               "ornament-vpadding" "0.25spc"
                                               "padding-above" "0.15fn"
                                               "padding-below" "0.15fn"
                                               "large-padding-above" "0.2fn"
                                               "large-padding-below" "0.2fn"
                                           (ornamented
                                               (with "par-par-sep" "0fn"
                                                     "par-sep" "0fn"
                                                 (document
                                                   (with "font-size" "0.8" "color" "blue"
                                                     (concat (action ,(string-append "[Source: " filename "]") ,btn-cmd)))
                                                 ,@(map (lambda (st)
                                                          (vault-transclude-rebase-images
                                                           (vault-strip-labels st)
                                                           source-dir))
                                                        (map tree->stree content)))))))))
                                (vault-transclude-error uuid-str f-hint-str b-hint-str e-hint-str "Target file missing.")))
                          (vault-transclude-error uuid-str f-hint-str b-hint-str e-hint-str "UUID not in database.")))))
            (lambda (key . args)
              (display* "Transclude error: " key " " args "\n")
              (set! res (vault-transclude-error uuid-str f-hint-str b-hint-str e-hint-str "Internal error."))))
          (set! active-transclusions (list-difference active-transclusions (list uuid-str)))
          res))))

(define (vault-common-prefix l1 l2)
  (cond ((or (null? l1) (null? l2)) '())
        ((== (car l1) (car l2))
         (cons (car l1) (vault-common-prefix (cdr l1) (cdr l2))))
        (else '())))

(define (vault-subtree t path)
  (if (null? path) t
      (vault-subtree (tree-ref t (car path)) (cdr path))))

(define (vault-list-tail l k)
  (if (<= k 0) l
      (if (null? l) '()
          (vault-list-tail (cdr l) (- k 1)))))

(define (vault-stree-children->trees st)
  (if (and (pair? st) (== (car st) 'document))
      (map stree->tree (cdr st))
      (list (stree->tree st))))

(define (vault-extract-whole t)
  (let* ((st (tree->stree t))
         (body (and (tmfile? st) (tmfile-extract st 'body))))
    (cond (body (vault-stree-children->trees body))
          ((and (tree-compound? t) (== (tree-label t) 'document))
           (tree-children t))
          (else (list t)))))

(define (vault-heading-anchor-id? s)
  (and (>= (string-length s) 3)
       (char=? (string-ref s 0) #\H)
       (char>=? (string-ref s 1) #\1)
       (char<=? (string-ref s 1) #\6)
       (char=? (string-ref s 2) #\space)))

(define (vault-heading-after-label t p)
  (let* ((prefix (reverse (cdr (reverse p))))
         (parent (if (null? prefix) t (vault-subtree t prefix)))
         (rem (vault-list-tail p (length prefix)))
         (first (if (null? rem) (tree-arity parent) (+ (car rem) 1))))
    (let loop ((i first))
      (if (>= i (tree-arity parent)) #f
          (let ((child (tree-ref parent i)))
            (cond ((and (tree-atomic? child)
                        (== (tm-string-trim-both (tree->string child)) ""))
                   (loop (+ i 1)))
                  ((vault-anchor-heading? (tree->stree child)) (list child))
                  (else #f)))))))

(define (vault-extract-range t b e)
  (if (and (string-null? b) (string-null? e))
      (vault-extract-whole t)
      (let* ((p-begin (tree-search-label t b))
             (p-end (if (string-null? e) #f (tree-search-label t e))))
        (if (and p-begin (or p-end (string-null? e)))
            (let ((heading (and p-end (== b e)
                                (vault-heading-anchor-id? b)
                                (vault-heading-after-label t p-begin))))
              (if heading heading
                  (let* ((prefix (if p-end
                                     (vault-common-prefix p-begin p-end)
                                     (reverse (cdr (reverse p-begin))))))
                    (let* ((parent (if (null? prefix) t
                                       (vault-subtree t prefix)))
                           (rem-b (vault-list-tail p-begin (length prefix)))
                           (rem-e (if p-end
                                      (vault-list-tail p-end (length prefix))
                                      '()))
                           (i-begin (if (null? rem-b) 0 (car rem-b)))
                           (i-end (if (or (not p-end) (null? rem-e))
                                      (- (tree-arity parent) 1)
                                      (car rem-e)))
                           (res '()))
                      (if (<= i-begin i-end)
                          (for (i i-begin (+ i-end 1))
                            (let ((child (tree-ref parent i)))
                              (set! res (append res (list child)))))
                          '())
                      res))))
            '()))))

(define (tree-search-label t lab)
  (if (string-null? lab) #f
      (let* ((pred? (lambda (node)
                      (and (tree-compound? node)
                           (== (tree-label node) 'label)
                           (>= (tree-arity node) 1)
                           (== (tree->string (tree-ref node 0)) lab))))
             (indices (tree-search-indices t pred?)))
        (if (pair? indices) (car indices) #f))))

(define (vault-transclude-error uuid f-hint b-hint e-hint msg)
  (let ((cmd (string-append "(vault-transclude-repair "
                            (object->string uuid) " "
                            (object->string f-hint) " "
                            (object->string b-hint) " "
                            (object->string e-hint) ")")))
    `(with "color" "red"
       (concat (bold "Broken Transclusion: ") ,msg " "
               (action "Repair" ,cmd)))))

(tm-define (wikilink-repair-apply
 bad-uuid new-uuid path anchor-begin anchor-end)
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
         (uuid (if (pair? parts) (vault-url-component-decode (car parts)) ""))
         (file-hint (if (and (pair? parts) (pair? (cdr parts)))
                        (vault-url-component-decode (cadr parts)) ""))
         (anchor-hint (if (and (pair? parts) (pair? (cdr parts)) (pair? (cddr parts)))
                          (vault-url-component-decode (list->tmfs (cddr parts))) ""))
         (node (vault-get-node uuid)))
    (display* "  UUID: " uuid ", hints: " file-hint ", " anchor-hint "\n")
    (display* "  Node: " node "\n")
    
    (if (and (tree? node) (== (tree-label node) 'tuple))
        (let* ((rel-path (tree->string (tree-ref node 0)))
               (a-begin (tree->string (tree-ref node 1)))
               (a-end (tree->string (tree-ref node 2)))
               (abs-url (url-append (vault-get-root) (unix->url rel-path))))
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
                `(document (TeXmacs ,(texmacs-compat-version)) 
                           (style (tuple "generic")) 
                           (body (document "Redirecting..."))))
              (begin
                (display* "  Target file missing on disk, triggering repair...\n")
                (wikilink-trigger-repair uuid file-hint anchor-hint))))
        (begin
          (display* "  UUID not found in database, triggering repair...\n")
          (wikilink-trigger-repair uuid file-hint anchor-hint)))))

(tmfs-load-handler (Wikilink name)
  (wikilink-handler-sub name))

(tmfs-load-handler (wikilink name)
  (wikilink-handler-sub name))

(tmfs-load-handler (artifact name)
  (if (artifact-open-uuid name)
      `(document
         (TeXmacs ,(texmacs-compat-version))
         (style (tuple "generic"))
         (body (document "Opening artifact...")))
      `(document
         (TeXmacs ,(texmacs-compat-version))
         (style (tuple "generic"))
         (body (document (bold "Artifact not found: ") ,name)))))

(tmfs-title-handler (artifact name doc)
  "Artifact")

(tmfs-permission-handler (artifact name kind)
  (== kind "read"))

(define (artifact-url? u)
  (when (string? u) (set! u (system->url u)))
  (url-rooted-tmfs-protocol? u "artifact"))

(define (artifact-url-uuid u)
  (when (string? u) (set! u (system->url u)))
  (url->system (url-tail u)))

;; tmfs://artifact/... is a navigation command, not a document.  Dispatch it
;; before the generic TMFS loader so following an artifact never leaves an
;; "Opening artifact..." buffer behind.  The C++ locator reuses the current
;; buffer when it already contains the defining source.
(tm-define (go-to-url u . opt-from)
  (:require (artifact-url? u))
  (when (pair? opt-from) (cursor-history-add (car opt-from)))
  (let ((target (artifact-resolve-uuid (artifact-url-uuid u))))
    (if target
        (begin
          (artifact-jump-to-position (car target) (cadr target))
          (when (pair? opt-from) (cursor-history-add (cursor-path))))
        (set-message
          "Artifact not found or its definition has changed; rebuild artifacts"
          "Artifact"))))

(tmfs-load-handler (artifact-disambiguation name)
  (tree->stree (artifact-disambiguation-page name)))

(tmfs-title-handler (artifact-disambiguation name doc)
  "Artifact disambiguation")

(tmfs-permission-handler (artifact-disambiguation name kind)
  (== kind "read"))

(define (wikilink-trigger-repair uuid file-hint anchor-hint)
  (display* "Trigger repair for " uuid ", hint: " file-hint "\n")
  (if (string-null? file-hint)
      `(document (TeXmacs ,(texmacs-compat-version)) (style (tuple "generic")) (body (document (bold "Error: ") "Broken Wikilink and no file hint provided.")))
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
                `(document (TeXmacs ,(texmacs-compat-version)) (style (tuple "generic")) (body (document (bold "Error: ") "Could not find any matches for: " ,file-hint))))
              (begin
                (display* "  Returning repair page with " (length candidates) " items\n")
                (wikilink-repair-page uuid file-hint anchor-hint (reverse candidates))))))))

(define (wikilink-repair-page uuid f-hint a-hint candidates)
  `(document
     (TeXmacs ,(texmacs-compat-version))
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

(register-preference-callback-procedures
  (list notify-cursor-color notify-document-background-color notify-enunciation-color notify-focus-border-width notify-focus-color notify-labels-mode notify-link-color notify-selection-color notify-vault-explorer-track notify-vault-preferences-mode))
