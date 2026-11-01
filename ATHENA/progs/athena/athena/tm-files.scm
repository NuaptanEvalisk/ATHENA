
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
  (:use (kernel athena tm-document)
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

(tm-define (propose-name-buffer)
  (with name (url->unix (current-buffer))
    (cond ((not (url-scratch? name)) name)
          ((os-win32?) "")
          (else (string-append (var-eval-system "pwd") "/")))))

(tm-property (choose-file fun text type)
  (:interactive #t))

(tm-define (open-auxiliary aux body . opt-master)
  (open-auxiliary-document
    aux body (if (null? opt-master) (buffer-master) (car opt-master))))

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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Autosave
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (more-recent file suffix1 suffix2)
  (and (url-exists? (url-glue file suffix1))
       (url-exists? (url-glue file suffix2))
       (url-newer? (url-glue file suffix1) (url-glue file suffix2))))

(define (most-recent-suffix file)
  (if (more-recent file "~" "") "~" ""))

(define (autosave-eligible? name)
  (and (not (url-rooted-web? name))
       (or (not (url-rooted-tmfs? name))
           (tmfs-autosave name "~"))))

(define (autosave-propose name)
  (and (autosave-eligible? name)
       (with s (most-recent-suffix name)
         (and (!= s "")
              (url-glue name s)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Opening files using external tools
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Choosing how links to non-native local files are opened
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (linked-file-format name)
  (format-from-suffix (locase-all (url-suffix name))))

(define (linked-file-convertible? name)
  (let* ((fm (linked-file-format name))
         (from (string-append fm "-document")))
    (and (!= fm "generic")
         (converter-search from "texmacs-tree"))))

(define (linked-file-opened name after-open)
  (when (and after-open (buffer-exists? name))
    (exec-buffer name after-open)))

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
    (with choices
      (append (if convertible?
                  '("Convert to ATHENA document" "convert") '())
              '("Edit as plain text" "plain"
                "Open with system application" "system"))
      (with answer (native-linked-file-choice (url->system name) choices)
        (cond ((== answer "convert") (linked-file-convert name after-open))
              ((== answer "plain") (linked-file-edit-plain name after-open))
              ((== answer "system") (load-external name)))))))

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
    ;; Loading is coordinated from the global/UI owner.  The idle predicate,
    ;; however, is editor state and must remain on the loaded buffer's actor.
    (exec-buffer name
      (lambda ()
        (delayed (:idle 1000)
          (google-cloud-todo-sync-buffer (current-buffer))))))
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
               (if (buffer-create-source name)
                   (set-message `(concat "Could not create " ,vname)
                                "Load file")
                   (begin
                     (load-buffer-open name opts)
                     ;; Registry creation/opening is global/UI-owned, but style
                     ;; initialization mutates the editor and therefore belongs
                     ;; to the newly created buffer's BufferActor.
                     (exec-buffer name (lambda () (buffer-set-default-style)))
                     (set-message `(concat "Could not load " ,vname
                                           ". Created new document")
                                  "Load file"))))))))

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
        (let* ((question "Load more recent autosave file?")
               ;; Interactive answers resume through the current view and may
               ;; therefore run on its BufferActor.  Keep only a detached
               ;; string across that continuation; buffer creation, registry
               ;; lookup, and opening remain global/UI-owned operations.
               (name* (url->string name)))
          (user-confirm question #t
            (lambda (answ)
              (exec-global
                (lambda ()
                  (let ((name (string->url name*)))
                    (if answ
                        (let* ((autosave-name (autosave-propose name))
                               (format (url-format name)))
                          ;; Preserve the persisted document-model version.
                          ;; In particular, a v2 autosave must activate the
                          ;; same owner-local identity index as a normal load.
                          (if (buffer-import name autosave-name format)
                              (set-message "Could not load autosave file"
                                           "Load file")
                              (begin
                                (load-buffer-open name opts)
                                (buffer-pretend-modified name))))
                        (load-buffer-check-permissions name opts))))))))
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
  (if (== (locase-all (url-suffix name)) "avd")
      (compound-document-open name)
      (load-buffer-check-autosave name opts)))

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
          (switch-to-buffer name)
          (apply load-buffer-main (cons name (cons :new-window opts)))))))

(tm-define (load-browse-buffer name . opt-after-open)
  (:synopsis "Load a buffer or switch to it if already open")
  ;; Resolve relative links in their source buffer before leaving its actor.
  ;; Only a detached string crosses into the GUI registry owner.
  (let* ((target (if (and (not (url-rooted? name)) (current-buffer))
                     (url-relative (current-buffer) name) name))
         (text (string-copy (url->system target))))
    (exec-global
      (lambda ()
        (apply load-browse-buffer-main
               (cons (system->url text) opt-after-open))))))

(define (load-browse-buffer-main name . opt-after-open)
  (let ((after-open (and (pair? opt-after-open) (car opt-after-open))))
    (cond ((url-rooted-protocol? name "mailto") (load-external name))
          ((== (locase-all (url-suffix name)) "avd")
           (compound-document-open name))
          ((buffer-exists? name)
           (switch-to-buffer name)
           (linked-file-opened name after-open))
          ((and (not (url-rooted? name)) (current-buffer))
           (apply load-browse-buffer
                  (cons (url-relative (current-buffer) name) opt-after-open)))
          ((or (url-rooted-web? name) (url-rooted-tmfs? name))
           (linked-file-load-native name after-open))
          ((format-image? (format-from-suffix (url-suffix name)))
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
;; Important files to which the buffer is linked
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (linked-file-list)
  (list))

(register-preference-callback-procedures
  (list notify-autosave))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; UI-independent rules migrated from retired Scheme menu modules
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (buffer-more-recent? b1 b2)
  (>= (buffer-last-visited b1)
      (buffer-last-visited b2)))

(tm-define (buffer-sorted-list)
  (with l (list-filter (buffer-list) buffer-in-menu?)
    (list-sort l buffer-more-recent?)))
