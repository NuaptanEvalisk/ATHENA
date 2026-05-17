(texmacs-module (athena athena tm-vault)
  (:use (kernel boot abbrevs)
        (kernel library list)
        (kernel library tree)
        (kernel athena tm-define)
        (kernel athena tm-file-system)
        (kernel athena tm-secure)
        (utils library cursor)
        (generic document-edit)
        (link ref-edit)
        (athena athena tm-vault-images)
        (athena menus file-menu)))

(tm-define (vault-jump-to-source path anchor)
  (load-buffer path)
  (if (!= anchor "")
      (delayed (:idle 100) (go-to-label anchor))))

(tm-define (vault-load-latest-action path-s)
  (load-vault-dir (string->url path-s)))

(tm-define (go-to-welcome-page)
  (load-buffer "tmfs://welcome/home"))

(define (vault-startup-unavailable-message dir-s)
  (show-message
   (string-append "Last vault is unavailable:\n" dir-s)
   "Vault unavailable"))

(define (vault-startup-available? dir)
  (url-exists? (url-append dir "Vaultfile")))

(define (vault-startup-open-welcome?)
  (== (get-preference "vault welcome page") "on"))

(define (vault-startup-show-explorer?)
  (== (get-preference "vault explorer show on startup") "on"))

(define (vault-track-current-buffer-if-enabled)
  (if (and (== (get-preference "vault explorer track current file") "on")
           (vault-active?)
           (current-buffer))
      (vault-explorer-track-file (current-buffer))))

(define (vault-show-explorer-and-track)
  (vault-show-explorer)
  (vault-track-current-buffer-if-enabled))

(define (vault-startup-show-explorer)
  (if (vault-startup-show-explorer?)
      (delayed (:idle 100)
        (if (vault-active?) (vault-show-explorer-and-track)))))

(tm-define (vault-startup-open-initial-buffer)
  (let* ((auto-load? (== (get-preference "vault auto load last") "on"))
         (report-missing? (== (get-preference "vault report missing last") "on"))
         (recent-vaults (get-recent-vaults))
         (latest-vault (if (pair? recent-vaults) (car recent-vaults) #f)))
    (cond
      ((not auto-load?)
       (if (vault-startup-open-welcome?) (go-to-welcome-page)))
      ((not latest-vault) #f)
      (else
       (let ((dir (string->url latest-vault)))
         (if (vault-startup-available? dir)
             (begin
               (load-vault-dir dir)
               (vault-startup-show-explorer)
               (if (vault-startup-open-welcome?) (go-to-welcome-page)))
             (if report-missing?
                 (vault-startup-unavailable-message latest-vault))))))))

(tm-define (ext-get-preference key def)
  (let ((val (get-preference key)))
    (if (string-null? val) def val)))

(define-secure-symbols wikilink-repair-apply vault-jump-to-source 
                       load-buffer load-vault-dir 
                       string->url vault-load-latest-action
                       go-to-welcome-page
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
  "$ATHENA_HOME_PATH/system/preferences.scm")

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

(define (vaultfile-with-preferences data prefs-path)
  (list (car data) (cadr data) prefs-path))

(define (vault-preferences-url dir prefs-path)
  (url-append dir prefs-path))

(define (vault-preferences-ensure-file dir vault-file data)
  (let* ((rel (vaultfile-preferences-path data))
         (prefs-rel (if (string-null? rel) "vprefs.scm" rel))
         (prefs-file (vault-preferences-url dir prefs-rel)))
    (when (string-null? rel)
      (save-object vault-file (vaultfile-with-preferences data prefs-rel)))
    (when (not (url-exists? prefs-file))
      (cpp-dump-preferences prefs-file))
    prefs-file))

(define (vault-preferences-activate dir vault-file data)
  (let ((prefs-file (vault-preferences-ensure-file dir vault-file data)))
    (set! vault-preferences-file prefs-file)
    (set! vault-preferences-active? #t)
    (set! vault-preferences-reloading? #t)
    (load-preferences-from prefs-file)
    (set! vault-preferences-reloading? #f)))

(define (vault-preferences-activate-current)
  (if (vault-active?)
      (let* ((dir (vault-get-root))
             (vault-file (url-append dir "Vaultfile"))
             (data (if (url-exists? vault-file) (load-object vault-file) '())))
        (if (and (list? data) (>= (length data) 2))
            (vault-preferences-activate dir vault-file data)))))

(define (vault-preferences-deactivate)
  (when vault-preferences-active?
    (save-preferences)
    (set! vault-preferences-active? #f)
    (set! vault-preferences-file #f)
    (set! vault-preferences-reloading? #t)
    (load-preferences-from (vault-system-preferences-file))
    (set! vault-preferences-reloading? #f)))

(define-preferences
  ("vault fuzzy search limit" "3" noop)
  ("vault transclusion color" "#f8f8f8" noop)
  ("gui cursor color" "red" notify-cursor-color)
  ("gui selection color" "red" notify-selection-color)
  ("vault welcome page" "on" noop)
  ("vault take preferences with vault" "off" notify-vault-preferences-mode)
  ("vault auto load last" "off" noop)
  ("vault report missing last" "off" noop)
  ("vault explorer show on startup" "on" noop)
  ("vault explorer track current file" "off" notify-vault-explorer-track)
  ("vault explorer use system trash" "off" noop)
  ("vault labels mode" "visible" notify-labels-mode)
  ("vault theorem color" "none" notify-enunciation-color)
  ("vault lemma color" "none" notify-enunciation-color)
  ("vault corollary color" "none" notify-enunciation-color)
  ("vault proposition color" "none" notify-enunciation-color)
  ("vault axiom color" "none" notify-enunciation-color)
  ("vault definition color" "none" notify-enunciation-color)
  ("vault notation color" "none" notify-enunciation-color)
  ("vault convention color" "none" notify-enunciation-color)
  ("vault conjecture color" "none" notify-enunciation-color)
  ("vault law color" "none" notify-enunciation-color)
  ("vault remark color" "none" notify-enunciation-color)
  ("vault note color" "none" notify-enunciation-color)
  ("vault example color" "none" notify-enunciation-color)
  ("vault warning color" "none" notify-enunciation-color)
  ("vault disambiguation color" "none" notify-enunciation-color)
  ("vault acknowledgments color" "none" notify-enunciation-color)
  ("vault exercise color" "none" notify-enunciation-color)
  ("vault problem color" "none" notify-enunciation-color)
  ("vault question color" "none" notify-enunciation-color)
  ("vault solution color" "none" notify-enunciation-color)
  ("vault answer color" "none" notify-enunciation-color)
  ("vault proof color" "none" notify-enunciation-color)
  ("vault proof alternative color" "none" notify-enunciation-color)
  ("vault proof standard color" "none" notify-enunciation-color))

(define (get-fuzzy-limit)
  (let ((pref (get-preference "vault fuzzy search limit")))
    (or (string->number pref) 3)))

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
                (equal? (get-preference "vault explorer use system trash") "on"))))
    (dynamic (vault-image-preferences-widget))))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vault management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (interactive-new-vault dir)
  (interactive (lambda (name)
                 (let* ((vault-file (url-append dir "Vaultfile"))
                        (db-path "map.tmdb"))
                   (save-object vault-file (list name db-path))
                   (vault-load dir name db-path)
                   (if (vault-take-preferences?)
                       (vault-preferences-activate dir vault-file
                                                   (list name db-path)))
                   (set-message (string-append "Created vault: " name) "Vault")))
               '("Vault name" "string")))

(tm-define (load-vault-dir dir)
  (let* ((vault-file (url-append dir "Vaultfile")))
    (if (url-exists? vault-file)
        (let ((data (load-object vault-file)))
          (if (and (list? data) (>= (length data) 2))
              (let ((take-prefs? (vault-take-preferences?)))
                (vault-preferences-deactivate)
                (vault-load dir (car data) (cadr data))
                (if take-prefs?
                    (vault-preferences-activate dir vault-file data))
                (add-recent-vault dir)
                (set-message (string-append "Loaded vault: " (car data)) "Vault"))
              (set-message "Invalid Vaultfile" "Error")))
        (interactive-new-vault dir))))

(tm-define (open-vault)
  (choose-file load-vault-dir "Load Vault" "directory"))

(tm-define (unload-vault)
  (:interactive #t)
  (if (vault-active?)
      (begin
        (vault-preferences-deactivate)
        (vault-close)
        (set-message "Unloaded vault" "Vault"))
      (set-message "No active vault to unload" "Vault")))

(define (vault-maintenance-binary)
  (url-append (get-texmacs-path) "bin/ATHENA.bin"))

(define (vault-maintenance-save-open-buffers)
  (for-each
   (lambda (buf)
     (when (and (not (buffer-aux? buf)) (buffer-modified? buf))
       (save-buffer buf)))
   (buffer-list)))

(define (vault-maintenance-launch-command root)
  (let* ((bin (url->system (vault-maintenance-binary)))
         (root-s (url->system root))
         (bin-q (escape-shell bin))
         (root-q (escape-shell root-s)))
    (string-append "( sleep 1; cd " root-q " && "
                   bin-q " --vault-maintenance " root-q " ) &")))

(tm-define (vault-maintenance)
  (:interactive #t)
  (cond ((not (vault-active?))
         (set-message "No active vault to maintain" "Vault maintenance"))
        ((not (url-exists? (vault-maintenance-binary)))
         (set-message "Cannot find ATHENA binary for vault maintenance"
                      "Vault maintenance"))
        (else
         (user-confirm
          "Vault maintenance will save open documents, quit ATHENA, back up the vault, and normalize image names. Continue?"
          #f
          (lambda (answ)
            (when answ
              (vault-maintenance-save-open-buffers)
              (let ((unsaved (filter (lambda (buf)
                                       (and (not (buffer-aux? buf))
                                            (buffer-modified? buf)))
                                     (buffer-list))))
                (if (nnull? unsaved)
                    (set-message "Some documents could not be saved; vault maintenance was not started"
                                 "Vault maintenance")
                    (begin
                      (system (vault-maintenance-launch-command
                               (vault-get-root)))
                      (quit-TeXmacs))))))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Recent Vaults
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (recent-vaults-file)
  "$ATHENA_HOME_PATH/system/recent_vaults.scm")

(tm-define (get-recent-vaults)
  (let ((file (recent-vaults-file)))
    (if (url-exists? file)
        (let ((data (load-object file)))
          (if (list? data) data '()))
        '())))

(tm-define (add-recent-vault dir)
  (let* ((dir-s (url->string dir))
         (old (get-recent-vaults))
         (new (cons dir-s (list-difference old (list dir-s))))
         (final (sublist new 0 (min (length new) 20))))
    (save-object (recent-vaults-file) final)))

(tm-menu (recent-vault-menu)
  (for (dir-s (get-recent-vaults))
    (let* ((u (string->url dir-s))
           (name (url->system (url-tail u)))
           (v-name `(verbatim ,name))
           (v-dir `(verbatim ,dir-s)))
      ((balloon (eval v-name) (eval v-dir))
       (load-vault-dir u)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Quick switcher
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (vault-root-base)
  (url-append (vault-get-root) ""))

(define (vault-url-in-current-vault? u)
  (and (url? u)
       (url-descends? u (vault-root-base))))

(define (vault-rel-path u)
  (url->unix (url-delta (vault-root-base) u)))

(define (vault-recent-ath-files)
  (let ((res '())
        (cur (current-buffer)))
    (if (and (vault-url-in-current-vault? cur)
             (url-exists? cur)
             (== (url-suffix cur) "ath"))
        (set! res (list (vault-rel-path cur))))
    (for (u (recent-file-list 200))
      (if (and (vault-url-in-current-vault? u)
               (url-exists? u)
               (== (url-suffix u) "ath")
               (not (in? (vault-rel-path u) res)))
          (set! res (append res (list (vault-rel-path u))))))
    res))

(define (vault-unsafe-path? path)
  (or (string-null? path)
      (string-starts? path "/")
      (string-starts? path "~")
      (in? ".." (string-tokenize-by-char path #\/))))

(define (vault-ensure-ath-suffix path)
  (if (string-ends? path ".ath") path (string-append path ".ath")))

(define (vault-current-file-directory)
  (let ((buf (current-buffer)))
    (if (and (vault-url-in-current-vault? buf)
             (== (url-suffix buf) "ath"))
        (url-head buf)
        (vault-get-root))))

(define (vault-ensure-directory dir)
  (if (url-exists? dir)
      #t
      (let ((parent (url-head dir)))
        (if (and (url? parent) (!= parent dir) (not (url-exists? parent)))
            (vault-ensure-directory parent))
        (system-mkdir dir))))

(define (vault-empty-ath-document)
  (stree->tree
   `(document
      (TeXmacs ,(texmacs-compat-version))
      (style (tuple "generic"))
      (body (document "")))))

(define (vault-create-and-open-ath query)
  (let* ((path (vault-ensure-ath-suffix (tm-string-trim-both query))))
    (if (vault-unsafe-path? path)
        (show-message "Invalid file name for quick switcher creation." "Quick switcher")
        (let* ((target (url-append (vault-current-file-directory) (unix->url path)))
               (dir (url-head target)))
          (vault-ensure-directory dir)
          (if (and (not (url-exists? target))
                   (tree-export (vault-empty-ath-document) target "texmacs"))
              (show-message "Could not create ATHENA file." "Quick switcher")
              (load-buffer target))))))

(tm-define (open-quick-switcher)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Quick switcher")
      (let ((res (vault-quick-switcher (vault-recent-ath-files))))
        (if (and (tree? res) (== (tree-label res) 'tuple) (>= (tree-arity res) 2))
            (let ((action (tree->string (tree-ref res 0)))
                  (payload (tree->string (tree-ref res 1))))
              (cond ((== action "open")
                     (load-buffer (url-append (vault-get-root) (unix->url payload))))
                    ((== action "create")
                     (vault-create-and-open-ath payload))))))))

(tm-define (open-vault-explorer)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Vault Explorer")
      (vault-show-explorer-and-track)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vault bugcheck
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define vault-bugcheck-run-id 0)
(define vault-bugcheck-errors '())
(define vault-bugcheck-load-timeout 120)
(define vault-bugcheck-settle-ticks 20)

(define (vault-bugcheck-wrap-line line width)
  (let loop ((s line) (wrapped '()))
    (if (<= (string-length s) width)
        (reverse (cons s wrapped))
        (loop (substring s width (string-length s))
              (cons (substring s 0 width) wrapped)))))

(define (vault-bugcheck-wrap-text text)
  (let loop ((lines (string-decompose text "\n")) (wrapped '()))
    (if (null? lines)
        (if (null? wrapped)
            ""
            (apply string-append (list-intersperse (reverse wrapped) "\n")))
        (loop (cdr lines)
              (append (reverse (vault-bugcheck-wrap-line (car lines) 96))
                      wrapped)))))

(define (vault-bugcheck-add-error rel kind detail)
  (let ((entry (string-append rel "\n  " kind ": " detail)))
    (set! vault-bugcheck-errors
          (append vault-bugcheck-errors (list entry)))))

(define (vault-bugcheck-numbered-errors errors)
  (let loop ((todo errors) (index 1) (out '()))
    (if (null? todo)
        (reverse out)
        (loop (cdr todo) (+ index 1)
              (cons (string-append (number->string index) ". " (car todo))
                    out)))))

(define (vault-bugcheck-summary total)
  (let* ((count (length vault-bugcheck-errors))
         (header (if (== count 0)
                     (string-append "Vault bugcheck completed.\n"
                                    (number->string total)
                                    " .ath files checked.\nNo errors found.")
                     (string-append "Vault bugcheck completed.\n"
                                    (number->string total)
                                    " .ath files checked.\n"
                                    (number->string count)
                                    " error(s) found:\n\n")))
         (body (if (== count 0)
                   ""
                   (apply string-append
                          (list-intersperse
                           (vault-bugcheck-numbered-errors vault-bugcheck-errors)
                           "\n\n")))))
    (string-append header body)))

(define (vault-bugcheck-log-summary summary)
  (catch #t
    (lambda ()
      (string-save summary
                   (url-append (vault-get-root)
                               (unix->url "VaultBugcheck.log"))))
    (lambda (key . args)
      (display* "Vault bugcheck could not write log: "
                key ", " args "\n"))))

(tm-widget ((vault-bugcheck-report-widget msg) done)
  (padded
    (resize '("480px" "760px" "1100px") '("260px" "520px" "760px")
      (scrollable
        (for (line (string-decompose msg "\n"))
          (hlist // (text line) >>)))
      ===
      (bottom-buttons >> ("Ok" (done))))))

(define (vault-bugcheck-show-summary total)
  (let ((summary (vault-bugcheck-summary total)))
    (display* summary "\n")
    (vault-bugcheck-log-summary summary)
    (dialogue-window
     (vault-bugcheck-report-widget (vault-bugcheck-wrap-text summary))
     noop
     "Vault Bugcheck")))

(define (vault-ath-files-recursive dir)
  (let* ((files (url-read-directory dir "*.ath"))
         (subdirs (list-sort
                   (list-filter (url-read-directory dir "*") url-directory?)
                   (lambda (a b) (string<=? (url->unix a) (url->unix b)))))
         (res files))
    (for (d subdirs)
      (let ((name (url->unix (url-tail d))))
        (if (not (string-starts? name "."))
            (set! res (append res (vault-ath-files-recursive d))))))
    (list-sort res (lambda (a b) (string<=? (url->unix a) (url->unix b))))))

(define (vault-bugcheck-rel u)
  (url->unix (url-delta (vault-root-base) u)))

(define (vault-bugcheck-report-error u key args)
  (let ((rel (vault-bugcheck-rel u)))
    (display* "Vault bugcheck failed while loading " rel ": "
              key ", " args "\n")
    (vault-bugcheck-add-error
     rel "load error" (object->string (cons key args)))))

(define (vault-bugcheck-load-file u)
  (catch #t
    (lambda ()
      (clear-debug-messages)
      (load-buffer u)
      #t)
    (lambda (key . args)
      (vault-bugcheck-report-error u key args)
      #f)))

(define (vault-bugcheck-debug-messages)
  (tree-children (get-debug-messages "Error messages" 1000)))

(define (vault-bugcheck-format-debug-message m)
  (let ((channel (tree->string (tree-ref m 0)))
        (message (tree->string (tree-ref m 1))))
    (string-append channel ": " message)))

(define (vault-bugcheck-current-file? u)
  (and (current-buffer)
       (== (current-buffer-url) u)))

(define (vault-bugcheck-force-typeset u)
  (catch #t
    (lambda ()
      (if (vault-bugcheck-current-file? u)
          (update-forced))
      #t)
    (lambda (key . args)
      (vault-bugcheck-report-error u key args)
      #f)))

(define (vault-bugcheck-after-load files total index run-id u)
  (when (== run-id vault-bugcheck-run-id)
    (catch #t
      (lambda ()
        (let ((rel (vault-bugcheck-rel u)))
          (vault-bugcheck-force-typeset u)
          (let ((messages (vault-bugcheck-debug-messages))
                (dups (duplicate-labels)))
            (for (m messages)
              (let ((message (vault-bugcheck-format-debug-message m)))
                (display* "Vault bugcheck found TeXmacs diagnostic in "
                          rel ": " message "\n")
                (vault-bugcheck-add-error rel "TeXmacs diagnostic" message)))
            (when dups
              (display* "Vault bugcheck found duplicate labels in " rel "\n")
              (vault-bugcheck-add-error
               rel "duplicate labels"
               (string-append (number->string (length dups))
                              " duplicate label occurrence(s)"))))
          (vault-bugcheck-step (cdr files) total (+ index 1) run-id)))
      (lambda (key . args)
        (vault-bugcheck-report-error u key args)
        (vault-bugcheck-step (cdr files) total (+ index 1) run-id)))))

(define (vault-bugcheck-wait-loaded files total index run-id u attempts-left)
  (when (== run-id vault-bugcheck-run-id)
    (cond
      ((vault-bugcheck-current-file? u)
       (vault-bugcheck-settle files total index run-id u
                              vault-bugcheck-settle-ticks))
      ((<= attempts-left 0)
       (vault-bugcheck-report-error
        u 'timeout
        (list "Timed out waiting for file to become the current buffer"))
       (vault-bugcheck-step (cdr files) total (+ index 1) run-id))
      (else
       (delayed (:pause 100)
         (vault-bugcheck-wait-loaded files total index run-id u
                                     (- attempts-left 1)))))))

(define (vault-bugcheck-settle files total index run-id u ticks-left)
  (when (== run-id vault-bugcheck-run-id)
    (cond
      ((not (vault-bugcheck-current-file? u))
       (vault-bugcheck-wait-loaded files total index run-id u
                                   vault-bugcheck-load-timeout))
      ((<= ticks-left 0)
       (vault-bugcheck-after-load files total index run-id u))
      (else
       (delayed (:pause 100)
         (vault-bugcheck-settle files total index run-id u
                                (- ticks-left 1)))))))

(define (vault-bugcheck-step files total index run-id)
  (when (== run-id vault-bugcheck-run-id)
    (if (null? files)
        (begin
          (display* "Vault bugcheck completed: " total " files checked\n")
          (vault-bugcheck-show-summary total))
        (let* ((u (car files))
               (rel (vault-bugcheck-rel u))
               (msg (string-append "Checking "
                                   (number->string index) "/"
                                   (number->string total) ": " rel)))
          (display* "Vault bugcheck: " msg "\n")
          (set-message msg "Vault Bugcheck")
          (if (vault-bugcheck-load-file u)
              (vault-bugcheck-wait-loaded files total index run-id u
                                          vault-bugcheck-load-timeout)
              (vault-bugcheck-step (cdr files) total (+ index 1) run-id))))))

(tm-define (vault-bugcheck)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Vault Bugcheck")
      (let ((files (vault-ath-files-recursive (vault-get-root))))
        (set! vault-bugcheck-run-id (+ vault-bugcheck-run-id 1))
        (if (null? files)
            (show-message "No .ath files found in the current vault." "Vault Bugcheck")
            (begin
              (clear-debug-messages)
              (set! vault-bugcheck-errors '())
              (display* "Vault bugcheck starting with "
                        (length files) " files\n")
              (vault-bugcheck-step files (length files) 1
                                   vault-bugcheck-run-id))))))

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

(kbd-commands
  ("=" "Insert Wikilink" (if (in-text?) (insert-wikilink)))
  ("+" "Insert Transclusion" (if (in-text?) (insert-transclude))))


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
                                           (ornamented
                                               (document
                                                 (with "font-size" "0.8" "color" "blue"
                                                   (concat (action ,(string-append "[Source: " filename "]") ,btn-cmd)))
                                               ,@(map (lambda (st)
                                                        (vault-transclude-rebase-images
                                                         (vault-strip-labels st)
                                                         source-dir))
                                                      (map tree->stree content))))))))
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

(define (vault-extract-whole t)
  (if (and (tree-compound? t) (== (tree-label t) 'document))
      (tree-children t)
      (list t)))

(define (vault-extract-range t b e)
  (if (and (string-null? b) (string-null? e))
      (vault-extract-whole t)
      (let* ((p-begin (tree-search-label t b))
             (p-end (if (string-null? e) #f (tree-search-label t e))))
        (if (and p-begin (or p-end (string-null? e)))
            (let* ((prefix (if p-end
                               (vault-common-prefix p-begin p-end)
                               (reverse (cdr (reverse p-begin))))))
              (let* ((parent (if (null? prefix) t (vault-subtree t prefix)))
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
                res))
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
  `(with "color" "red"
     (concat (bold "Broken Transclusion: ") ,msg " "
             (action "Repair" "(insert-transclude)"))))

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

(tmfs-load-handler (welcome name)
  (let* ((recent-files (recent-file-list 15))
         (recent-vaults (get-recent-vaults))
         (latest-vault (if (pair? recent-vaults) (car recent-vaults) #f)))
    (tm->stree
      `(document
         (TeXmacs ,(texmacs-compat-version))
         (style (tuple "generic"))
         (body (document
           (with "par-mode" "center"
             (document
               (with "font-size" "2" "font-series" "bold"
                 (concat "Welcome to " (ATHENA)))
               (with "font-size" "1.2" "font-shape" "italic"
                 "Advanced Typesetting and Hypertext Environment for Notes and Archives")))
           (vspace "2fn")

           (section* "Quick Start")
           (enumerate (document
             (concat (item) (action "Open Blank Buffer" "(new-document)"))
             ,@(if latest-vault
                   `((concat (item) (action ,(string-append "Load Latest Vault (" (url->system (url-tail (string->url latest-vault))) ")")
                                            ,(string-append "(vault-load-latest-action " (object->string latest-vault) ")"))))
                   '())))

           (vspace "1fn")
           (section* "Recent Files")
           (enumerate (document
             ,@(map (lambda (u)
                    `(concat (item) (action ,(url->system u) ,(string-append "(load-buffer " (object->string (url->string u)) ")"))))
                  recent-files)))

           (vspace "2fn")
           (with "font-size" "0.8" "color" "grey"
             (document
               (with "par-mode" "center"
                 (document
                   "ATHENA is a fork based on GNU TeXmacs."
                   (concat "Copyright " (copyright) " 1999-2026 Joris van der Hoeven and others.")
                   (concat "Copyright " (copyright) " 2026 Nuaptan F. Evalisk.")
                   "Released under the GNU General Public License version 3 or later."))))
           ))
         (initial (collection
           (associate "font-base-size" "7")
           (associate "font-family" "tt")
           (associate "page-medium" "automatic")))))))
(tmfs-load-handler (Wikilink name)
  (wikilink-handler-sub name))

(tmfs-load-handler (wikilink name)
  (wikilink-handler-sub name))

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
