
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

(tm-property (load-buffer name . opts)
  (:argument name smart-file "File name")
  (:default name (propose-name-buffer)))

(tm-property (load-buffer-in-new-window name . opts)
  (:argument name smart-file "File name")
  (:default name (propose-name-buffer)))

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
