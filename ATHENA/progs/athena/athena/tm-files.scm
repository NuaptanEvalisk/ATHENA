
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : files.scm
;; DESCRIPTION : file handling
;; COPYRIGHT   : (C) 2001-2021  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-files)
  (:use (athena athena tm-server)
        (athena athena tm-view)
        (athena athena tm-print)
        (athena athena tm-vault-anchors)
        (kernel athena tm-convert)
        (kernel athena tm-dialogue)
        (utils library cursor)))
(import-from (kernel athena tm-preferences))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Supplementary routines on urls, taking into account the TeXmacs file system
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define cpp-url-last-modified url-last-modified)
(define cpp-url-newer? url-newer?)
(define cpp-buffer-last-save buffer-last-save)

(tm-define (url-last-modified u)
  (if (url-rooted-tmfs? u)
      (tmfs-date u)
      (cpp-url-last-modified u)))

(tm-define (url-newer? u1 u2)
  (if (or (url-rooted-tmfs? u1) (url-rooted-tmfs? u2))
      (and-let* ((d1 (url-last-modified u1))
                 (d2 (url-last-modified u2)))
        (> d1 d2))
      (cpp-url-newer? u1 u2)))

(tm-define (url-remove u)
  (if (url-rooted-tmfs? u)
      (tmfs-remove u)
      (system-remove u)))

(tm-define (url-autosave u suf)
  (if (url-rooted-tmfs? u)
      (tmfs-autosave u suf)
      (and (or (url-scratch? u)
               (url-test? u "fw")
               (and (not (url-exists? u))
                    (url-test? u "c")))
           (url-glue u suf))))

(tm-define (url-wrap u)
  (and (url-rooted-tmfs? u)
       (tmfs-wrap u)))

(tm-define (buffer-last-save u)
  (with base (url-wrap u)
    (cond ((not base)
           (cpp-buffer-last-save u))
          ((buffer-exists? base)
           (buffer-last-save base))
          (else (url-last-modified base)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Miscellaneous subroutines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (buffer-missing-style?)
  (with t (tree->stree (get-style-tree))
    (and (pair? t) (== (car t) 'tuple) (null? (cdr t)))))

(tm-define (buffer-set-default-style)
  (init-style "generic")
  (with psz (get-printer-paper-type)
    (if (!= psz "a4") (init-page-type psz)))
  (with type (get-preference "page medium")
    (if (!= type "papyrus") (init-env "page-medium" type)))
  (when (!= (get-preference "scripting language") "none")
    (lazy-plugin-force)
    (init-env "prog-scripts" (get-preference "scripting language")))
  (buffer-pretend-saved (current-buffer)))

(tm-define (propose-name-buffer)
  (with name (url->unix (current-buffer))
    (cond ((not (url-scratch? name)) name)
          ((os-win32?) "")
          (else (string-append (var-eval-system "pwd") "/")))))

(tm-property (choose-file fun text type)
  (:interactive #t))

(tm-define (open-auxiliary aux body . opt-master)
  (let* ((name (aux-name aux))
         (master (if (null? opt-master) (buffer-master) (car opt-master))))
    (aux-set-document aux body)
    (aux-set-master aux master)
    (switch-document name)))

(define-public-macro (with-aux u . prg)
  `(let* ((u ,u)
          (t (tree-import u "texmacs"))
          (name (current-buffer))
          (aux "* Aux *"))
     (aux-set-document aux t)
     (aux-set-master aux u)
     (switch-to-buffer (aux-name aux))
     (with r (begin ,@prg)
       (switch-to-buffer name)
       r)))

(tm-define (buffer-copy buf u)
  (:synopsis "Creates a copy of @buf in @u and return @u")
  (with-buffer buf
    (let* ((styles (get-style-list))
           (init (get-all-inits))
           (refl (list-references))
           (refs (map get-reference refl))
           (body (tree-copy (buffer-get-body buf))))
      (view-new u) ; needed by buffer-focus, used in with-buffer
      (buffer-set-body u body) 
      (with-buffer u
        (set-style-list styles)
        (init-env "global-title" (buffer-get-metadata buf "title"))
        (init-env "global-author" (buffer-get-metadata buf "author"))
        (init-env "global-subject" (buffer-get-metadata buf "subject"))
        (for-each
         (lambda (t)
           (if (tree-func? t 'associate)
               (with (var val) (list (tree-ref t 0) (tree-ref t 1))
                 (init-env-tree (tree->string var) val))))
         (tree-children init))
        (for-each set-reference refl refs))
      u)))

(tm-define (switch-to-buffer* buf)
  (cond ((== buf (current-buffer)) (noop))
        ((nnull? (buffer->windows buf))
         (switch-to-window (car (buffer->windows buf))))
        (else (switch-to-buffer buf)))
  (schedule-persistent-fit-width))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Saving buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define current-save-source (url-none))
(tm-define current-save-target (url-none))

(tm-define (autosave-file? name)
  (and (url? name)
       (not (url-rooted-tmfs? name))
       (with s (url->system name)
         (or (string-ends? s "~")
             (string-ends? s "#")))))

(define (buffer-notify-recent name)
  (when (not (autosave-file? name))
    (learn-interactive 'recent-buffer (list (cons "0" (url->unix name))))
    (save-learned)))

(define (has-faithful-format? name)
  (in? (url-suffix name) '("ath" "tm" "ts" "tp" "stm" "tmml" "scm" "")))

(define (save-buffer-preserve-current-viewport name)
  (when (and (current-view)
             (== (url->url name) (url->url (current-buffer))))
    ;; THE_FREEZE: apply save-related tree updates without scrolling the cursor.
    (notify-change 512)))

(define (save-buffer-post name opts)
  ;;(display* "save-buffer-post " name "\n")
  (when (defined? 'google-cloud-todo-sync-buffer)
    (delayed (:idle 0) (google-cloud-todo-sync-buffer name)))
  (when (defined? 'vault-backup-dispatch-realtime)
    (vault-backup-dispatch-realtime name)))

(define (save-buffer-save-now name opts)
  ;;(display* "save-buffer-save " name "\n")
  (with vname `(verbatim ,(utf8->cork (url->system name)))
    (save-buffer-preserve-current-viewport name)
    (vault-backup-pre-save name)
    (if (buffer-save name)
        (begin
          (buffer-pretend-modified name)
          (set-message `(concat "Could not save " ,vname) "Save file"))
        (begin
          (if (== (url-suffix name) "ts") (style-clear-cache))
          (autosave-remove name)
          (buffer-notify-recent name)
          (set-message `(concat "Saved " ,vname) "Save file")
          (save-buffer-post name opts)))))

(define (save-buffer-save name opts)
  (if (in? :manual opts)
      (vault-anchor-before-manual-save
       name
       (lambda () (save-buffer-save-now name opts)))
      (save-buffer-save-now name opts)))

(define (save-buffer-check-faithful name opts)
  ;;(display* "save-buffer-check-faithful " name "\n")
  (if (has-faithful-format? name)
      (save-buffer-save name opts)
      (user-confirm "Save requires data conversion. Really proceed?" #f
        (lambda (answ)
          (when answ
            (save-buffer-save name opts))))))

(define (cannot-write? name action)
  (with vname `(verbatim ,(url->system name))
    (cond ((and (not (url-test? name "f")) (not (url-test? name "c")))
           (with msg `(concat "The file " ,vname " cannot be created")
             (set-message msg action))
           #t)
          ((and (url-test? name "f") (not (url-test? name "w")))
           (with msg `(concat "You do not have write access for " ,vname)
             (set-message msg action))
           #t)
          (else #f))))

(define (save-buffer-check-permissions name opts)
  ;;(display* "save-buffer-check-permissions " name "\n")
  (save-buffer-preserve-current-viewport name)
  (set! current-save-source name)
  (set! current-save-target name)
  (with vname `(verbatim ,(url->system name))
    (cond ((url-scratch? name)
           (choose-file
	    (lambda (x) (apply save-buffer-as-main
		(cons x (if (x-gui?) opts (cons :overwrite opts)))))
	      "Save ATHENA file" "texmacs"))
          ((not (buffer-exists? name))
           (with msg `(concat "The buffer " ,vname " does not exist")
             (set-message msg "Save file")))
          ((not (buffer-modified? name))
           (with msg "No changes need to be saved"
             (set-message msg "Save file"))
           (save-buffer-post name opts))
          ((cannot-write? name "Save file")
           (noop))
          ((and (url-test? name "fr")
                (and-with mod-t (url-last-modified name)
                  (and-with save-t (buffer-last-save name)
                    (> mod-t save-t))))
           (user-confirm "The file has changed on disk. Really save?" #f
             (lambda (answ)
               (when answ
                 (save-buffer-check-faithful name opts)))))
          (else (save-buffer-check-faithful name opts)))))

(tm-define (save-buffer-main . args)
  ;;(display* "save-buffer-main\n")
  (if (or (null? args) (not (url? (car args))))
      (save-buffer-check-permissions (current-buffer) args)
      (save-buffer-check-permissions (car args) (cdr args))))

(tm-define (save-buffer . l)
  (apply save-buffer-main l))

(tm-define (save-buffer-manual . l)
  (apply save-buffer-main (append l '(:manual))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Saving buffers under a new name
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (save-buffer-as-save new-name name opts)
  ;;(display* "save-buffer-as-save " new-name ", " name "\n")
  (if (and (url-scratch? name) (url-exists? name)) (system-remove name))
  (buffer-rename name new-name)
  (buffer-pretend-modified new-name)
  (save-buffer-save new-name opts))

(define (save-buffer-as-check-faithful new-name name opts)
  ;;(display* "save-check-as-check-faithful " new-name ", " name "\n")
  (if (or (== (url-suffix new-name) (url-suffix name))
          (has-faithful-format? new-name))
      (save-buffer-as-save new-name name opts)
      (user-confirm "Save requires data conversion. Really proceed?" #f
        (lambda (answ)
          (when answ
            (save-buffer-as-save new-name name opts))))))

(define (save-buffer-as-check-other new-name name opts)
  ;;(display* "save-buffer-as-check-other " new-name ", " name "\n")
  (cond ((buffer-exists? new-name)
         (with s (string-append "The file " (url->system new-name)
                                " is being edited. Discard edits?")
           (user-confirm s #f
             (lambda (answ)
               (when answ (save-buffer-as-save new-name name opts))))))
        (else (save-buffer-as-save new-name name opts))))

(define (save-buffer-as-check-permissions new-name name opts)
  ;;(display* "save-buffer-as-check-permissions " new-name ", " name "\n")
  (cond ((cannot-write? new-name "Save file")
         (noop))
        ((and (url-test? new-name "f") (nin? :overwrite opts))
         (user-confirm "File already exists. Really overwrite?" #f
           (lambda (answ)
             (when answ (save-buffer-as-check-other new-name name opts)))))
        (else (save-buffer-as-check-other new-name name opts))))

(tm-define (save-buffer-as-main new-name . args)
  ;;(display* "save-buffer-as-main " new-name "\n")
  (if (or (null? args) (not (url? (car args))))
      (save-buffer-as-check-permissions new-name (current-buffer) args)
      (save-buffer-as-check-permissions new-name (car args) (cdr args))))

(tm-define (save-buffer-as new-name . args)
  (:argument new-name texmacs-file "Save as")
  (:default  new-name (propose-name-buffer))
  (when (string? new-name)
    (set! new-name (string-replace new-name ":" "-"))
    (set! new-name (string-replace new-name ";" "-")))
  (with opts (if (x-gui?) args (cons :overwrite args))
    (apply save-buffer-as-main (cons new-name opts))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Exporting buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (export-buffer-export name to fm opts)
  ;;(display* "export-buffer-export " name ", " to ", " fm "\n")
  (with vto `(verbatim ,(url->system to))
    (if (if (and (== fm "latex") (in? :latex-portable opts))
            (with-converter-option
             "texmacs-stree" "latex-document"
             "texmacs->latex:portable" "on"
             (lambda () (buffer-export name to fm)))
            (buffer-export name to fm))
        (set-message `(concat "Could not save " ,vto) "Export file")
        (set-message `(concat "Exported to " ,vto) "Export file"))))

(define (export-buffer-check-permissions name to fm opts)
  ;;(display* "export-buffer-check-permissions " name ", " to ", " fm "\n")
  (cond ((cannot-write? to "Export file")
         (noop))
        ((and (url-test? to "f") (nin? :overwrite opts))
         (user-confirm "File already exists. Really overwrite?" #f
           (lambda (answ)
             (when answ (export-buffer-export name to fm opts)))))
        (else (export-buffer-export name to fm opts))))

(tm-define (export-buffer-main name to fm opts)
  ;;(display* "export-buffer-main " name ", " to ", " fm "\n")
  (when (and (pair? to) (url? (car to)))
    (when (and (nnull? (cdr to)) (== (cadr to) "on"))
      (set! opts (cons :latex-portable opts)))
    (set! to (car to)))
  (when (string? to)
    (set! to (string-replace to ":" "-"))
    (set! to (string-replace to ";" "-"))
    (set! to (url-relative (buffer-get-master name) to)))
  (if (url? name) (set! current-save-source name))
  (if (url? to) (set! current-save-target to))
  (export-buffer-check-permissions name to fm opts))

(tm-define (export-buffer to)
  (with fm (url-format to)
    (if (== fm "generic") (set! fm "verbatim"))
    (export-buffer-main (current-buffer) to fm (list :overwrite))))

(tm-define (buffer-exporter fm)
  (with opts (if (x-gui?) (list) (list :overwrite))
    (lambda (s) (export-buffer-main (current-buffer) s fm opts))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Autosave
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define autosave-buffer-state (make-ahash-table))

(define (autosave-key name)
  (url->system name))

(define (autosave-default-enabled?)
  (== (get-preference "autosave default") "on"))

(define (autosave-real-file? name)
  (and (url? name)
       (not (url-rooted-web? name))
       (not (url-rooted-tmfs? name))
       (not (url-scratch? name))
       (url-exists? name)
       (url-test? name "fw")))

(tm-define (autosave-buffer-enabled? name)
  (and (autosave-real-file? name)
       (with state (ahash-ref autosave-buffer-state (autosave-key name))
         (if state (== state "on") (autosave-default-enabled?)))))

(tm-define (current-buffer-autosave-enabled?)
  (autosave-buffer-enabled? (current-buffer)))

(tm-define (toggle-autosave-current-buffer)
  (:check-mark "v" current-buffer-autosave-enabled?)
  (let ((name (current-buffer)))
    (if (not (autosave-real-file? name))
        (set-message "Autosave is only available for on-disk files"
                     "Auto-save file")
        (let ((enabled? (not (autosave-buffer-enabled? name))))
          (ahash-set! autosave-buffer-state (autosave-key name)
                      (if enabled? "on" "off"))
          (if enabled? (autosave-delayed))
          (set-message (if enabled? "Autosave enabled" "Autosave disabled")
                       "Auto-save file")))))

(define (more-recent file suffix1 suffix2)
  (and (url-exists? (url-glue file suffix1))
       (url-exists? (url-glue file suffix2))
       (url-newer? (url-glue file suffix1) (url-glue file suffix2))))

(define (most-recent-suffix file)
  (if (more-recent file "~" "")
      (if (not (more-recent file "#" "")) "~"
          (if (more-recent file "#" "~") "#" "~"))
      (if (more-recent file "#" "") "#" "")))

(define (autosave-eligible? name)
  (and (not (url-rooted-web? name))
       (or (not (url-rooted-tmfs? name))
           (tmfs-autosave name "~"))))

(define (autosave-propose name)
  (and (autosave-eligible? name)
       (with s (most-recent-suffix name)
         (and (!= s "")
              (url-glue name s)))))

(define (autosave-rescue? name) 
  (and (autosave-eligible? name)
       (== (most-recent-suffix name) "#")))

(define (autosave-remove name)
  (when (url-exists? (url-glue name "~"))
    (url-remove (url-glue name "~")))
  (when (url-exists? (url-glue name "#"))
    (url-remove (url-glue name "#"))))

(tm-define (autosave-buffer name)
  (when (and (buffer-modified-since-autosave? name)
             (autosave-buffer-enabled? name)
             (url-autosave name "~"))
    ;;(display* "Autosave " name "\n")
    ;; FIXME: incorrectly autosaves after cursor movements only
    (let* ((vname `(verbatim ,(url->system name)))
           (suffix (if (rescue-mode?) "#" "~"))
           (aname (url-autosave name suffix))
           (fm (url-format name)))
      (if (url-scratch? name) (set! aname name))
      (cond ((nin? fm (list "texmacs" "stm"))
             (when (not (rescue-mode?))
               (set-message `(concat "Warning: " ,vname " not auto-saved")
                            "Auto-save file")))
            ((buffer-export name aname fm)
             (when (not (rescue-mode?))
               (set-message `(concat "Failed to auto-save " ,vname)
                            "Auto-save file")))
            (else
             (when (not (rescue-mode?))
               (buffer-pretend-autosaved name)
               (set-temporary-message `(concat "Auto-saved " ,vname)
                                      "Auto-save file" 2500)))))))

(tm-define (autosave-all)
  (for-each autosave-buffer (buffer-list)))

(tm-define (autosave-now)
  (autosave-all)
  (autosave-delayed))

(tm-define (autosave-delayed)
  (let* ((pref (get-preference "autosave"))
         (len (if (and (string? pref) (integer? (string->number pref)))
                  (* (string->number pref) 1000) 120000)))
    (if (> len 0)
        (delayed
          (:pause len)
          (autosave-now)))))

(define (notify-autosave var val)
  (if (current-view) ; delayed-autosave would crash at initialization time
      (autosave-delayed)))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Opening files using external tools
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (buffer-external? u)
  (or (and (url-rooted-web? u)
           ;; FIXME: Use HTTP HEADERS to determine the real file format
           (!= (file-format u) "texmacs-file"))
      ;; (url-directory? u)
      ;; we want to open links to directories via the default OS handler,
      ;; but we need a silent test which does not call concretize
      (url-rooted-protocol? u "mailto")
      (file-of-format? u "image")
      (file-of-format? u "pdf")
      (file-of-format? u "postscript")
      (file-of-format? u "generic")))

(tm-define (default-open)
  (cond ((os-macos?) "open")
        ((or (os-mingw?) (os-win32?)) "start")
        (else "xdg-open")))

(tm-define (load-external u)
  (when (not (url-rooted? u))
    (set! u (url-relative (current-buffer) u)))
  (cond ((url-rooted-protocol? u "doi")
         (with u* (url-append (root->url "https")
                              (url-append (string->url "www.doi.org")
                                          (url-unroot u)))
           (load-external u*)))
        ((url-rooted-protocol? u "mailto")
         (system (string-append (default-open) " " (url->string u))))
        ((not (url-rooted-web? u))
         (system-1 (default-open) u))
        ((os-mingw64?)
         (eval-system (url->system u)))
        ((or (os-mingw?) (os-win32?))
         (system (string-append (default-open) " " (url->system u))))
        (else
         (system (string-append (default-open) " "
                                (raw-quote (url->system u)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Choosing how links to non-native local files are opened
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (linked-file-convertible-open-widget name cmd)
  (padded
    (vertical
      (text `(concat "How should ATHENA open "
                     (verbatim ,(url->system name)) "?"))
      ===
      (bottom-buttons
        ("Convert to ATHENA document" (cmd "convert"))
        // //
        ("Edit as plain text" (cmd "plain"))
        // //
        ("Open with system application" (cmd "system"))
        // //
        ("Cancel" (cmd "cancel"))))))

(tm-widget (linked-file-unknown-open-widget name cmd)
  (padded
    (vertical
      (text `(concat "How should ATHENA open "
                     (verbatim ,(url->system name)) "?"))
      ===
      (bottom-buttons
        ("Edit as plain text" (cmd "plain"))
        // //
        ("Open with system application" (cmd "system"))
        // //
        ("Cancel" (cmd "cancel"))))))

(define (linked-file-format name)
  (format-from-suffix (locase-all (url-suffix name))))

(define (linked-file-convertible? name)
  (let* ((fm (linked-file-format name))
         (from (string-append fm "-document")))
    (and (!= fm "generic")
         (converter-search from "texmacs-tree"))))

(define (linked-file-opened name after-open)
  (when (and after-open (buffer-exists? name)) (after-open)))

(define (linked-file-load-native name after-open)
  (load-buffer name)
  (linked-file-opened name after-open))

(define (linked-file-edit-plain name after-open)
  (if (buffer-exists? name)
      (begin
        (switch-to-buffer name)
        (linked-file-opened name after-open))
      (if (buffer-import name name "verbatim")
          (set-message
            `(concat "Could not open " (verbatim ,(url->system name))
                     " as plain text")
            "Open file")
          (begin
            (load-buffer-open name '())
            (linked-file-opened name after-open)))))

(define (linked-file-convert name after-open)
  (let* ((fm (linked-file-format name))
         (s (url->tmfs-string name))
         (converted (string->url
                      (string-append "tmfs://import/" fm "/" s))))
    (load-buffer converted)
    (linked-file-opened converted after-open)))

(define (linked-file-open-choice name after-open)
  (let ((convertible? (linked-file-convertible? name)))
    (dialogue-window
      (lambda (cmd)
        (if convertible?
            (linked-file-convertible-open-widget name cmd)
            (linked-file-unknown-open-widget name cmd)))
      (lambda (answer)
        (cond ((== answer "convert")
               (linked-file-convert name after-open))
              ((== answer "plain")
               (linked-file-edit-plain name after-open))
              ((== answer "system") (load-external name))))
      "Open linked file")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Loading buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (load-buffer-open name opts)
  ;;(display* "load-buffer-open " name ", " opts "\n")
  (cond ((in? :background opts) (noop))
        ((in? :new-window opts)
         (open-buffer-in-window name (buffer-get name) ""))
        (else
          (switch-to-buffer name)))
  (schedule-persistent-fit-width)
  (buffer-notify-recent name)
  (when (defined? 'google-cloud-todo-sync-buffer)
    (delayed (:idle 1000) (google-cloud-todo-sync-buffer name)))
  (when (tree-contains-compound? (buffer-get name)
                                 "gpg-passphrase-encrypted-buffer")
    (tm-gpg-dialogue-passphrase-decrypt-buffer name))
  (and-with master (and (url-rooted-tmfs? name) (tmfs-master name))
    (when (!= master name)
      (buffer-set-master name master)))
  (noop))

(define (load-buffer-load name opts)
  ;;(display* "load-buffer-load " name ", " opts "\n")
  (with vname `(verbatim ,(url->system name))
    (cond ((buffer-exists? name)
           (load-buffer-open name opts))
          ((url-exists? name)
           (if (buffer-load name)
               (set-message `(concat "Could not load " ,vname) "Load file")
               (load-buffer-open name opts)))
          (else
            (with uname (if (string? name) (string->url name) name)
              (buffer-set-body name '(document ""))
              (load-buffer-open name opts)
              (buffer-set-default-style)
              (set-message `(concat "Could not load " ,vname
                                    ". Created new document")
                           "Load file"))))))

(define (load-buffer-check-permissions name opts)
  ;;(display* "load-buffer-check-permissions " name ", " opts "\n")
  (let* ((file? (url-test? name "f"))
         (create? (or file? (url-test? name "c")))
         (read? (or (not file?) (url-test? name "r"))))
    (with vname `(verbatim ,(url->system name))
      (cond ((and (not file?) (not create?))
             (with msg `(concat "The file " ,vname
                                " cannot be loaded or created")
               (set-message msg "Load file")))
            ((and file? (not read?))
             (with msg `(concat "You do not have read access to " ,vname)
               (set-message msg "Load file")))
            (else (load-buffer-load name opts))))))

(define (load-buffer-check-autosave name opts)
  ;;(display* "load-buffer-check-autosave " name ", " opts "\n")
  (let ((proposal (autosave-propose name)))
    (if (and proposal (nin? :strict opts))
        (with question (if (autosave-rescue? name)
                           "Rescue file from crash?"
                           "Load more recent autosave file?")
          (user-confirm question #t
            (lambda (answ)
              (if answ
                  (let* ((autosave-name (autosave-propose name))
                         (format (url-format name))
                         (doc (tree-import autosave-name format)))
                    (buffer-set name doc)
                    (load-buffer-open name opts)
                    (buffer-pretend-modified name))
                  (load-buffer-check-permissions name opts)))))
        (load-buffer-check-permissions name opts))))

(tm-define (load-buffer-main name . opts)
  ;;(display* "load-buffer-main " name ", " opts "\n")
  (if (and (not (url-exists? name))
           (url-exists? (url-append "$ATHENA_FILE_PATH" name)))
      (set! name (url-resolve (url-append "$ATHENA_FILE_PATH" name) "f")))
  (if (not (url-rooted? name))
      (if (current-buffer)
          (set! name (url-relative (current-buffer) name))
          (set! name (url-append (url-pwd) name))))
  (load-buffer-check-autosave name opts))

(tm-define (load-buffer name . opts)
  (:argument name smart-file "File name")
  (:default  name (propose-name-buffer))
  ;;(display* "load-buffer " name ", " opts "\n")
  (exec-global
    (lambda () (apply load-buffer-main (cons name opts)))))

(tm-define (load-buffer-in-new-window name . opts)
  (:argument name smart-file "File name")
  (:default  name (propose-name-buffer))
  (exec-global
    (lambda ()
      (if (buffer->window name)
          (noop) ;;(window-focus (buffer->window name))
          (apply load-buffer-main (cons name (cons :new-window opts)))))))

(tm-define (load-browse-buffer name . opt-after-open)
  (:synopsis "Load a buffer or switch to it if already open")
  (let ((after-open (and (pair? opt-after-open) (car opt-after-open))))
    (cond ((url-rooted-protocol? name "mailto") (load-external name))
          ((buffer-exists? name)
           (switch-to-buffer name)
           (linked-file-opened name after-open))
          ((and (not (url-rooted? name)) (current-buffer))
           (apply load-browse-buffer
                  (cons (url-relative (current-buffer) name) opt-after-open)))
          ((or (url-rooted-web? name) (url-rooted-tmfs? name))
           (linked-file-load-native name after-open))
          ((or (file-of-format? name "image")
               (file-of-format? name "pdf")
               (file-of-format? name "postscript"))
           (load-external name))
          ((== (file-format name) "texmacs-file")
           (linked-file-load-native name after-open))
          ((url-test? name "f")
           (linked-file-open-choice name after-open))
          (else (linked-file-load-native name after-open)))))

(tm-define (open-buffer)
  (:synopsis "Open a new file")
  (choose-file load-buffer "Load file" ""))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Reverting buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (revert-buffer-revert . l)
  (with name (if (null? l) (current-buffer) (car l))
    (if (not (buffer-exists? name))
        (load-buffer name)
        (begin
          (when (!= name (current-buffer))
            (switch-to-buffer name))
          (url-cache-invalidate name)
          (with t (tree-import name (url-format name))
            (if (== t (tm->tree "error"))
                (set-message "Error: file not found" "Revert buffer")
                (buffer-set name t)))))))

(tm-define (revert-buffer . l)
  (with name (if (null? l) (current-buffer) (car l))
    (if (and (buffer-exists? name) (buffer-modified? name))
        (user-confirm "Buffer has been modified. Really revert?" #f
          (lambda (answ)
            (when answ (apply revert-buffer-revert l))))
        (apply revert-buffer-revert l))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Importing buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (latex-import-format? fm)
  (or (== fm "latex")
      (== fm "tex")
      (== fm "latex-document")))

(define (import-buffer-import-sub name fm opts)
  ;;(display* "import-buffer-import " name ", " fm "\n")
  (if (== fm (url-format name))
      (apply load-buffer-main (cons name opts))
      (let* ((s (url->tmfs-string name))
             (u (string-append "tmfs://import/" fm "/" s)))
        (apply load-buffer-main (cons u opts)))))

(define (import-buffer-import name fm opts)
  (if (latex-import-format? fm)
      (dynamic-wind
        (lambda () (system-wait "Importing LaTeX" "please wait"))
        (lambda () (import-buffer-import-sub name fm opts))
        (lambda () (system-wait "" "")))
      (import-buffer-import-sub name fm opts)))

(define (import-buffer-check-permissions name fm opts)
  ;;(display* "import-buffer-check-permissions " name ", " fm "\n")
  (with vname `(verbatim ,(url->system name))
    (cond ((not (url-test? name "f"))
           (with msg `(concat "The file " ,vname " does not exist")
             (set-message msg "Import file")))
          ((not (url-test? name "r"))
           (with msg `(concat "You do not have read access to " ,vname)
             (set-message msg "Import file")))
          (else (import-buffer-import name fm opts)))))

(tm-define (import-buffer-main name fm opts)
  ;;(display* "import-buffer-main " name ", " fm "\n")
  (if (and (not (url-exists? name))
           (url-exists? (url-append "$ATHENA_FILE_PATH" name)))
      (set! name (url-resolve (url-append "$ATHENA_FILE_PATH" name) "f")))
  (import-buffer-check-permissions name fm opts))

(tm-define (import-buffer name fm . opts)
  (import-buffer-main name fm opts))

(tm-define (buffer-importer fm)
  (lambda (s) (import-buffer s fm)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; System dependent conventions for buffer management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (open-in-window)
  (choose-file load-buffer-in-new-window "Load file" ""))

(tm-define (open-document)
  (open-buffer))

(tm-define (open-document*)
  (open-in-window))

(tm-define (load-document u)
  (:argument u smart-file "File name")
  (:default  u (propose-name-buffer))
  (when (not (url-none? u))
    (load-buffer u)))

(tm-define (load-document* u)
  (:argument u smart-file "File name")
  (:default  u (propose-name-buffer))
  (when (not (url-none? u))
    (load-buffer-in-new-window u)))

(tm-define (switch-document u)
  (:argument u smart-file "File name")
  (:default  u (propose-name-buffer))
  (when (not (url-none? u))
    (load-buffer u)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Printing buffers
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (printer-file-suffix)
  (if (and (supports-native-pdf?)
	   (get-boolean-preference "native pdf"))
      "pdf" "ps"))

(tm-define (printer-file-format)
  (if (and (supports-native-pdf?)
	   (get-boolean-preference "native pdf"))
      "pdf" "postscript"))

(tm-define (interactive-page-setup)
  (:synopsis "Specify the page setup")
  (:interactive #t)
  (set-message "Not yet implemented" "Printer setup"))

(tm-define (direct-print-buffer)
  (:synopsis "Print the current buffer")
  (print))

(tm-define (interactive-print-buffer)
  (:synopsis "Print the current buffer")
  (:interactive #t)
  (with file (url-append (url-temp-dir)
                         (string-append "tmpprint." (printer-file-suffix)))
    (print-to-file file)
    (interactive-print '() file)))

(tm-define (print-buffer)
  (:synopsis "Print the current buffer")
  (:interactive (use-print-dialog?))
  (if (use-print-dialog?)
      (interactive-print-buffer)
      (direct-print-buffer)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Important files to which the buffer is linked
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (linked-file-list)
  (list))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Deprecated functionality
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (set-abbr-buffer name abbr)
  (deprecated-function "set-abbr-buffer" "buffer-set-title")
  (buffer-set-title (current-buffer) abbr))

(tm-define (get-abbr-buffer name)
  (deprecated-function "get-abbr-buffer" "buffer-get-title")
  (buffer-get-title (current-buffer)))

(tm-define (set-buffer name doc)
  (deprecated-function "set-buffer" "buffer-set")
  (buffer-set name doc))

(tm-define (set-buffer-tree name doc)
  (deprecated-function "set-buffer-tree" "buffer-set")
  (set-buffer-tree name doc))

(tm-define (get-buffer-tree name)
  (deprecated-function "get-buffer-tree" "buffer-get-body")
  (get-buffer-tree name))

(tm-define (get-name-buffer-path p)
  (deprecated-function "get-name-buffer-path" "path->buffer")
  (path->buffer p))

(tm-define (get-name-buffer)
  (deprecated-function "get-name-buffer" "current-buffer")
  (current-buffer))

(tm-define (set-name-buffer name)
  (deprecated-function "set-name-buffer" "buffer-rename")
  (buffer-rename (current-buffer) name))

(tm-define (pretend-save-buffer)
  (deprecated-function "pretend-save-buffer" "buffer-pretend-saved")
  (buffer-pretend-saved (current-buffer)))

(tm-define (buffer-unsaved?)
  (deprecated-function "buffer-unsaved?" "buffer-modified?")
  (buffer-modified? (current-buffer)))

(tm-define (no-name?)
  (deprecated-function "no-name?" "buffer-has-name?")
  (not (buffer-has-name? (current-buffer))))

(tm-define (kill-buffer)
  (deprecated-function "kill-buffer" "buffer-close")
  (buffer-close (current-buffer)))

(register-preference-callback-procedures
  (list notify-autosave))
