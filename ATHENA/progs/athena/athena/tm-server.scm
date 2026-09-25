
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-server.scm
;; DESCRIPTION : server wide properties and resource management
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-server))
(import-from (kernel athena tm-preferences))
(lazy-define (generic document-edit) init-default set-document-language)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (get-default-show-table-cells)
  (if (qt-gui?) "on" "off"))

(define (notify-look-and-feel var val)
  (set-message "Restart in order to let the new look and feel take effect"
               "configure look and feel"))

(define (notify-security var val)
  (cond ((== val "accept no scripts") (set-script-status 0))
        ((== val "prompt on scripts") (set-script-status 1))
        ((== val "accept all scripts") (set-script-status 2))))

(define (notify-latex-command var val)
  (set-latex-command val))

(define (notify-tool var val)
  ;; FIXME: the menus sometimes don't get updated,
  ;; but the fix below does not work
  (when (current-view)
    (delayed (:idle 0) (notify-change 1))))

(define (notify-new-page-breaking var val)
  (noop))

(define (notify-enunciation-rendering var val)
  (refresh-now "enunciations"))

(tm-define (ext-render-exercises-smaller?)
  (:secure #t)
  (if (== (get-preference "render solution in smaller font") "on")
      "true"
      "false"))

(tm-define (ext-render-solution-smaller?)
  (:secure #t)
  (ext-render-exercises-smaller?))

(tm-define (ext-number-solutions?)
  (:secure #t)
  (if (== (get-preference "number solutions") "on") "true" "false"))

(tm-define (ext-render-exercise-diagnostic stage which body)
  (:secure #t)
  "")

(define (get-default-native-menubar)
  "off")

(define (get-default-unified-toolbar)
  "off")

(define athena-booted? #f)

(define (notify-restart . args)
  (when athena-booted?
    (notify-now "Restart ATHENA in order to let the new setting take effect")))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Properties of some built-in routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (symbol<=? s1 s2)
  (string<=? (symbol->string s1) (symbol->string s2)))

(define (get-function-list)
  (list-sort (%athena-defined-symbols) symbol<=?))

(define (get-interactive-function-list)
  (let* ((funs (get-function-list))
         (pred? (lambda (fun) (not (not (property fun :arguments))))))
    (list-filter funs pred?)))

(tm-define (exec-interactive-command cmd)
  (:argument  cmd "Interactive command")
  (:proposals cmd (cons "" (map symbol->string
                                (get-interactive-function-list))))
  (interactive (eval (string->symbol cmd))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Killing buffers, windows and TeXmacs
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (buffer-close name)
  (cpp-buffer-close name))

(tm-define (buffers-modified?)
  (list-or (map buffer-modified? (buffer-list))))

(define (discardable-blank-buffer? buf)
  (and (buffer-exists? buf)
       (url-scratch? buf)
       (tree-empty? (buffer-get-body buf))))

(define (buffer-needs-save-confirmation? buf)
  (and (buffer-modified? buf)
       (not (discardable-blank-buffer? buf))))

(define (quit-save-candidate-buffer? buf)
  (and (not (buffer-aux? buf))
       (not (string-starts? (url->string buf) "tmfs://"))))

(define (modified-quit-save-candidate-buffers)
  (filter (lambda (buf)
            (and (quit-save-candidate-buffer? buf)
                 (buffer-needs-save-confirmation? buf)))
          (buffer-list)))

(define (finish-ATHENA restart?)
  (exec-global
    (lambda ()
      (if restart?
          (unless (restart-TeXmacs)
            (notify-now "Could not restart ATHENA"))
          (quit-TeXmacs)))))

(define (save-selected-unsaved-buffers-and-finish buffers restart?)
  ;; GUI orchestration passes Scheme strings, not shared native URL trees.
  ;; The success continuation is deliberately not called on cancellation/error.
  (if (null? buffers)
      (finish-ATHENA restart?)
      (let* ((name (car buffers))
             (buf (url->url name))
             (next (lambda ()
                     (exec-global
                       (lambda ()
                         (save-selected-unsaved-buffers-and-finish
                          (cdr buffers) restart?))))))
        (if (or (not (buffer-exists? buf)) (not (buffer-modified? buf)))
            (next)
            (begin
              (switch-to-buffer buf)
              (unless (exec-buffer buf
                        (lambda ()
                          (save-buffer-manual (url->url name)
                                              (cons 'on-saved next))))
                (notify-now "Could not schedule buffer save")))))))

(define (ask-unsaved-close cont)
  (user-ask
    (list "The document has unsaved changes. What would you like to do?"
          "question" "Save and Close" "Close Without Saving" "Cancel")
    cont))

(define (close-buffer-by-name-global buf-name)
  (let ((buf (string->url buf-name)))
    (when (buffer-exists? buf) (buffer-close buf))))

(define (save-buffer-before-global-action buf-name action)
  (let* ((buf (string->url buf-name))
         (next (lambda () (exec-global action))))
    (cond ((not (buffer-exists? buf)) (noop))
          ((not (buffer-modified? buf)) (next))
          (else
            ;; Saving, including Save As for scratch buffers, remains owned by
            ;; the buffer actor.  The close/window action runs only after the
            ;; save path reports success through its on-saved continuation.
            (switch-to-buffer buf)
            (unless (exec-buffer buf
                      (lambda ()
                        (save-buffer-manual (url->url buf-name)
                                            (cons 'on-saved next))))
              (notify-now "Could not schedule buffer save"))))))

(define (handle-close-buffer-choice buf-name answer)
  (cond ((== answer "Save and Close")
         (exec-global
           (lambda ()
             (save-buffer-before-global-action
               buf-name
               (lambda () (close-buffer-by-name-global buf-name))))))
        ((== answer "Close Without Saving")
         (exec-global
           (lambda () (close-buffer-by-name-global buf-name))))))

(tm-define (safely-kill-buffer)
  (cond ((buffer-embedded? (current-buffer))
         (alt-windows-delete (alt-window-search (current-buffer))))
        ((buffer-needs-save-confirmation? (current-buffer))
         (let ((buf-name (url->string (current-buffer))))
           (ask-unsaved-close
             (lambda (answer)
               (handle-close-buffer-choice buf-name answer)))))
        (else (buffer-close (current-buffer)))))

(define (close-buffer-after-window buf)
  ;; Keep one passive buffer while ADS panes are the only remaining UI.
  ;; Several core paths assume that TeXmacs never has zero buffers.
  ;; A link may have reopened the document before delayed cleanup runs.
  (when (and (null? (buffer->windows buf))
             (or (> (windows-number) 0) (not (ads-open-panes?))))
    (buffer-close buf)))

(define (close-buffer-after-window-later buf)
  (let ((buf-name (url->string buf)))
    (delayed
      (:idle 100)
      (exec-global
        (lambda ()
          (close-buffer-after-window (string->url buf-name)))))))

(define (do-kill-window-global win buf)
  (kill-window win)
  (close-buffer-after-window-later buf))

(define (handle-close-window-choice win-name buf-name answer)
  (cond ((== answer "Save and Close")
         (exec-global
           (lambda ()
             (save-buffer-before-global-action
               buf-name
               (lambda ()
                 (do-kill-window-global
                   (string->url win-name) (string->url buf-name)))))))
        ((== answer "Close Without Saving")
         (exec-global
           (lambda ()
             (do-kill-window-global
               (string->url win-name) (string->url buf-name)))))))

(define (safely-kill-window-global win-name fallback-buf-name)
  (let* ((win (string->url win-name))
         (mapped (window->buffer win))
         (buf (if (url-none? mapped)
                  (string->url fallback-buf-name)
                  mapped))
         (win-name* (url->string win))
         (buf-name* (url->string buf)))
    (cond ((and (<= (windows-number) 1) (not (ads-open-panes?)))
           (safely-quit-ATHENA))
          ((buffer-needs-save-confirmation? buf)
           (ask-unsaved-close
             (lambda (answer)
               (handle-close-window-choice win-name* buf-name* answer))))
          (else (do-kill-window-global win buf)))))

(tm-define (safely-kill-window . opt-name)
  (if (and (buffer-embedded? (current-buffer)) (null? opt-name))
      (alt-windows-delete (alt-window-search (current-buffer)))
      (let* ((raw-win (if (null? opt-name) (current-window) (car opt-name)))
             (win-name (if (string? raw-win) raw-win (url->string raw-win)))
             (buf-name (url->string (current-buffer))))
        ;; Window tables, view mappings and ADS state are global/Qt-owned.
        ;; A source-bound close command transfers only Scheme strings and then
        ;; returns to its BufferActor before any window teardown is attempted.
        (exec-global
          (lambda () (safely-kill-window-global win-name buf-name))))))

(define (confirm-finish-ATHENA restart?)
  (let* ((l (modified-quit-save-candidate-buffers)))
    (if (null? l)
        (finish-ATHENA restart?)
        (begin
          (when (nin? (current-buffer) l)
            ;; FIXME: focus on window with buffer, if any
            (switch-to-buffer (car l)))
          (with result (native-unsaved-buffers (map url->string l) restart?)
            (when (pair? result)
              (cond ((== (car result) "save")
                     (save-selected-unsaved-buffers-and-finish
                       (cdr result) restart?))
                    ((== (car result) "discard")
                     (finish-ATHENA restart?)))))))))

(tm-define (safely-quit-ATHENA)
  (exec-global (lambda () (confirm-finish-ATHENA #f))))

(tm-define (safely-restart-ATHENA)
  (exec-global (lambda () (confirm-finish-ATHENA #t))))
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; System dependent conventions for buffer management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (new-document)
  (new-document-buffer))

(tm-define (new-document*)
  (open-document-window #f))

(tm-define (close-document)
  (delayed (:idle 1)
    (safely-kill-buffer)))

(tm-define (close-document*)
  (safely-kill-window))

(register-preference-callback-procedures
  (list notify-enunciation-rendering notify-latex-command notify-look-and-feel notify-new-page-breaking notify-restart notify-security notify-tool))
