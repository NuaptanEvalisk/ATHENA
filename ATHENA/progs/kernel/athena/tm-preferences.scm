
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-preferences.scm
;; DESCRIPTION : management of the user preferences
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel athena tm-preferences)
  (:use (kernel athena tm-define)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; C++ backed preference glue
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (preference->string x)
  (if (string? x) x (object->string x)))

(tm-define (register-preference-default which value)
  (cpp-register-preference which (preference->string value) (string? value)))

(define (preference-callback->string callback)
  (cond ((string? callback) callback)
        ((symbol? callback) (symbol->string callback))
        ((procedure? callback)
         (with name (procedure-name callback)
           (if name (symbol->string name) "")))
        (else "")))

(tm-define (register-preference-callback which callback)
  (cpp-register-preference-callback
    which (preference-callback->string callback)))

(define preference-callback-procedures (make-ahash-table))

(tm-define (register-preference-callback-procedure callback)
  (with name (preference-callback->string callback)
    (when (!= name "")
      (ahash-set! preference-callback-procedures name callback))))

(tm-define (notify-preference-callback callback)
  (with name (preference-callback->string callback)
    (when (!= name "")
      (for (which (cpp-preference-callbacks))
        (when (== (cpp-preference-callback which) name)
          (notify-preference which))))))

(tm-define (register-preference-callback-procedures callbacks)
  (for (callback callbacks)
    (register-preference-callback-procedure callback)
    (notify-preference-callback callback)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Setting and getting preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (test-preference? which what)
  (if (!= what "default")
      (== (get-preference which) what)
      (not (cpp-has-preference? which))))

(tm-define (set-preference which what)
  (:synopsis "Set preference @which to @what")
  (:check-mark "*" test-preference?)
  ;;(display* "set-preference " which " := " what "\n")
  (with val (if (string? what) what (object->string what))
    (when (!= (get-preference which) val)
      (cpp-set-preference which val)
      (save-preferences))))

(define (test-no-preference? which)
  (not (cpp-has-preference? which)))

(tm-define (reset-preference which)
  (:synopsis "Revert preference @which to default setting")
  (:check-mark "*" test-no-preference?)
  ;;(display* "reset-preference " which "\n")
  (when (cpp-has-preference? which)
    (cpp-reset-preference which)
    (save-preferences)))

(define (get-call-back what)
  (with name (cpp-preference-callback what)
    (if (== name "") (lambda args (noop))
        (let ((r (ahash-ref preference-callback-procedures name)))
          (if r r
            (let ((sym (string->symbol name)))
              (if (defined? sym)
              (catch #t
                (lambda ()
                  (with r (eval sym)
                    (if (procedure? r) r (lambda args (noop)))))
                (lambda err (lambda args (noop))))
              (lambda args (noop)))))))))

(tm-define (notify-preference which)
  (:synopsis "Notify that the preference @which was changed")
  ;;(display* "notify-preference " which ", " (get-preference which) "\n")
  ((get-call-back which) which (get-preference which)))

(tm-define (notify-all-preferences)
  (:synopsis "Notify that all preferences were reloaded")
  (for (which (cpp-preference-callbacks))
    (notify-preference which)))

(tm-define (ext-fold-toc-in-reflow?)
  (:secure #t)
  (if (== (get-preference "fold table of contents in reflow") "on")
      "true"
      "false"))

(tm-define (toc-fold-tree t)
  (:secure #t)
  (when (and (tree? t) (tree->path t)
             (toc-fold-set-path (tree->path t) #t))
    (noop)))

(tm-define (toc-unfold-tree t)
  (:secure #t)
  (when (and (tree? t) (tree->path t)
             (toc-fold-set-path (tree->path t) #f))
    (noop)))

(define (notify-fold-table-of-contents name val)
  (refresh-window))

(tm-define (load-preferences-from file)
  (:synopsis "Load user preferences from @file")
  (cpp-load-preferences file)
  (notify-all-preferences))

(tm-define (get-preference which)
  (:synopsis "Get preference @which")
  (let* ((s? (cpp-preference-default-string? which))
         (r (cpp-get-preference which "default")))
    (if s? r (string->object r))))

(tm-define (preference-on? which)
  (test-preference? which "on"))

(tm-define (toggle-preference which)
  (:synopsis "Toggle the preference @which")
  (:check-mark "v" preference-on?)
  (with what (get-preference which)
    (set-preference which (cond ((== what "on") "off")
                                ((== what "off") "on")
                                (else what)))))

(tm-define (append-preference which val)
  (:synopsis "Appends @val to the list of values of preference @which")
  (with cur (get-preference which)
    (if (== cur "default") (set! cur '()))
    (set-preference which (rcons cur val))))

(tm-define (set-boolean-preference which val)
  (set-preference which (if val "on" "off")))

(tm-define (get-boolean-preference which)
  (== (get-preference which) "on"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Complete preferences document
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (preference-export-metadata->entries fields)
  (if (null? fields) '()
      (cons (list (list-ref fields 0) (list-ref fields 1)
                  (list-ref fields 2) (list-ref fields 3)
                  (list-ref fields 4))
            (preference-export-metadata->entries (list-tail fields 5)))))

(define (preference-export-entry-field entry index)
  (list-ref entry index))

(define (preference-export-ordered-unique values)
  (if (null? values) '()
      (cons (car values)
            (preference-export-ordered-unique
              (filter (lambda (value) (!= value (car values)))
                      (cdr values))))))

(define (preference-export-value name redact?)
  (let ((value (cpp-get-preference name "default")))
    (if (and redact? (native-preference-sensitive? name) (!= value ""))
        "[REDACTED]"
        (if (== value "") "(empty)" value))))

(define (preference-export-row label name redact?)
  `(row
     (cell ,label)
     (cell (verbatim ,(preference-export-value name redact?)))
     (cell ,(if (cpp-has-preference? name) "Customized" "Default"))))

(define (preference-export-table entries redact?)
  `(tabular
     (tformat
       (twith "table-width" "1par")
       (twith "table-hmode" "exact")
       (cwith 1 1 1 -1 "cell-background" "#e8e8e8")
       (cwith 1 -1 1 -1 "cell-hyphen" "t")
       (cwith 1 -1 1 -1 "cell-lsep" "0.5em")
       (cwith 1 -1 1 -1 "cell-rsep" "0.5em")
       (cwith 1 -1 1 -1 "cell-tsep" "0.25em")
       (cwith 1 -1 1 -1 "cell-bsep" "0.25em")
       (cwith 1 -1 1 1 "cell-width" "0.36par")
       (cwith 1 -1 2 2 "cell-width" "0.44par")
       (cwith 1 -1 3 3 "cell-width" "0.20par")
       (table
         (row (cell (strong "Preference"))
              (cell (strong "Effective value"))
              (cell (strong "Source")))
         ,@(map (lambda (entry)
                  (preference-export-row
                    (preference-export-entry-field entry 3)
                    (preference-export-entry-field entry 4) redact?))
                entries)))))

(define (preference-export-group group tab entries redact?)
  (append (if (== group tab) '() `((subsubsection ,group)))
          (list (preference-export-table entries redact?))))

(define (preference-export-tab tab entries redact?)
  (let ((groups (preference-export-ordered-unique
                  (map (lambda (entry)
                         (preference-export-entry-field entry 2))
                       entries))))
    (cons `(subsection ,tab)
          (append-map
            (lambda (group)
              (preference-export-group
                group tab
                (filter (lambda (entry)
                          (== (preference-export-entry-field entry 2) group))
                        entries)
                redact?))
            groups))))

(define (preference-export-section category entries redact?)
  (let ((selected
          (filter (lambda (entry)
                    (== (preference-export-entry-field entry 0) category))
                  entries)))
    (let ((tabs (preference-export-ordered-unique
                  (map (lambda (entry)
                         (preference-export-entry-field entry 1))
                       selected))))
      (cons `(section ,category)
            (append-map
              (lambda (tab)
                (preference-export-tab
                  tab
                  (filter (lambda (entry)
                            (== (preference-export-entry-field entry 1) tab))
                          selected)
                  redact?))
              tabs)))))

(tm-define (view-all-preferences)
  (:interactive #t)
  (let ((choice (native-preferences-export-privacy)))
    (when (!= choice 0)
      (let* ((redact? (== choice 1))
             (entries (preference-export-metadata->entries
                        (native-preferences-export-metadata)))
             (categories (preference-export-ordered-unique
                           (map (lambda (entry)
                                  (preference-export-entry-field entry 0))
                                entries)))
             (sections
               (append-map
                 (lambda (category)
                   (preference-export-section category entries redact?))
                 categories))
             (mode (if redact? "Sensitive values are redacted."
                       "Sensitive values are included. Do not share this document."))
             (doc `(document
                     (doc-data
                       (doc-title (concat "All " (ATHENA) " Preferences")))
                     (concat
                       (strong "Export mode: ")
                       ,(if redact? mode `(with "color" "red" ,mode)))
                     (concat
                       "This document follows the category, tab, section, and setting order of the Preferences window. Values marked Default are inherited from "
                       (ATHENA)
                       "; Customized values are explicitly stored in the active preferences scope.")
                     ,@sections)))
        (new-buffer)
        (buffer-set-title (current-buffer)
                          (if redact? "All preferences (redacted)"
                              "All preferences"))
        (buffer-set-body (current-buffer) (stree->tree doc))))))

(define (notify-debug-backtrace name val)
  (if (== val "on")
      (when (not (in? 'backtrace (debug-options)))
        (if (guile-d?)
            (debug-enable 'backtrace)
            (debug-enable 'backtrace 'debug)))
      (when (in? 'backtrace (debug-options))
        (if (guile-d?)
            (debug-disable 'backtrace)
            (debug-disable 'backtrace 'debug)))))

(define (debug-memory-footer t)
  (let* ((s (tree->stree t))
         (a `(concat ,s " [" ,(number->string (texmacs-memory)) " bytes]")))
    (stree->tree a)))

(define (notify-debug-memory-footer name val)
  (set! footer-hook
        (if (== val "on") debug-memory-footer (lambda (s) s))))

(register-preference-callback-procedures
  (list notify-debug-backtrace notify-debug-memory-footer))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Look and feel
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-public (look-and-feel)
  (with s (get-preference "look and feel")
    (if (== s "default") (default-look-and-feel) s)))

(define (test-look-and-feel t)
  ;;(display* "Check look and feel " t "\n")
  (cond ((list? t) (list-or (map test-look-and-feel t)))
        ((symbol? t) (test-look-and-feel (symbol->string t)))
        ((and (string? t) (string-starts? t "no-"))
         (not (test-look-and-feel (substring t 3 (string-length t)))))
        (else
          (with s (look-and-feel)
            (or (== t s) (and (== t "std") (!= s "emacs")))))))

(define-public (use-popups?)
  #t)

(define-public (use-menus?)
  #f)

(define-public (use-print-dialog?)
  (and (qt-gui?) (== (get-preference "gui:print dialogue") "on")))

(set! has-look-and-feel? test-look-and-feel)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Notify that the Scheme preferences system has been started
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(notify-preferences-booted)
