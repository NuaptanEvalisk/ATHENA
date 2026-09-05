
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

(define (notify-scripting-language var val)
  (if (current-view)
      (if (== val "none")
          (init-default "prog-scripts")
          (init-env "prog-scripts" val))))

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

(define (notify-new-fonts var val)
  (set-new-fonts (== val "on")))

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

(tm-property (system cmd)
  (:argument cmd "System command"))

(tm-property (footer-eval cmd)
  (:argument cmd "Scheme command"))

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

(define (unsaved-buffer-display-name buf)
  (let ((s (url->system (url->url buf))))
    (if (== s "") buf s)))

(define (unsaved-buffer-set-selected selected buf flag)
  (cond (flag (if (in? buf selected) selected (cons buf selected)))
        ((in? buf selected) (list-remove selected buf))
        (else selected)))

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

(tm-widget ((unsaved-buffers-dialog buffers restart?) quit)
  (let ((selected buffers))
    (padded
      (resize '("560px" "760px" "1000px") '("280px" "420px" "700px")
        (vertical
          (text "The following buffers have unsaved changes:")
          ===
          (scrollable
            (for (buf buffers)
              (hlist
                (toggle (exec-global
                          (lambda ()
                            (set! selected
                                  (unsaved-buffer-set-selected
                                   selected buf answer))))
                        (in? buf selected))
                // //
                (text (unsaved-buffer-display-name buf))
                >>)))))
      ===
      (cond
        (restart?
         (bottom-buttons
           ("Save and restart"
            (exec-global
              (lambda ()
                (quit)
                (save-selected-unsaved-buffers-and-finish selected #t))))
           // //
           ("Restart" (exec-global (lambda () (quit) (finish-ATHENA #t))))
           // //
           ("Cancel" (exec-global (lambda () (quit))))))
        (else
         (bottom-buttons
           ("Save and exit"
            (exec-global
              (lambda ()
                (quit)
                (save-selected-unsaved-buffers-and-finish selected #f))))
           // //
           ("Exit" (exec-global (lambda () (quit) (finish-ATHENA #f))))
           // //
           ("Cancel" (exec-global (lambda () (quit))))))))))

(tm-define (safely-kill-buffer)
  (cond ((buffer-embedded? (current-buffer))
         (alt-windows-delete (alt-window-search (current-buffer))))
        ((buffer-needs-save-confirmation? (current-buffer))
         (user-confirm "The document has not been saved. Really close it?" #f  
           (lambda (answ)
             (when answ (buffer-close (current-buffer))))))
        (else (buffer-close (current-buffer)))))

(define (close-buffer-after-window buf)
  ;; Keep one passive buffer while ADS panes are the only remaining UI.
  ;; Several core paths assume that TeXmacs never has zero buffers.
  (when (or (> (windows-number) 0) (not (ads-open-panes?)))
    (buffer-close buf)))

(define (do-kill-window)
  (with buf (current-buffer)
    (kill-window (current-window))
    (delayed
      (:idle 100)
      (close-buffer-after-window buf))))

(define (do-kill-window* u)
 (with buf (window->buffer u)
   (kill-window u)
   (delayed
     (:idle 100)
     (close-buffer-after-window buf))))

(tm-define (safely-kill-window . opt-name)
  (cond ((and (buffer-embedded? (current-buffer)) (null? opt-name))
         (alt-windows-delete (alt-window-search (current-buffer))))
        ((and (<= (windows-number) 1) (not (ads-open-panes?)))
         (safely-quit-ATHENA))        ((nnull? opt-name)
         (with buf (window->buffer (car opt-name))
           (if (and buf (buffer-needs-save-confirmation? buf))
               (user-confirm
                   "The document has not been saved. Really close it?" #f
                 (lambda (answ)
                   (when answ (do-kill-window* (car opt-name)))))
               (do-kill-window* (car opt-name)))))
        ((buffer-needs-save-confirmation? (current-buffer))
         (user-confirm "The document has not been saved. Really close it?" #f
           (lambda (answ)
             (when answ (do-kill-window)))))
        (else (do-kill-window))))

(define (confirm-finish-ATHENA restart?)
  (let* ((l (modified-quit-save-candidate-buffers)))
    (if (null? l)
        (finish-ATHENA restart?)
        (begin
          (when (nin? (current-buffer) l)
            ;; FIXME: focus on window with buffer, if any
            (switch-to-buffer (car l)))
          (dialogue-window
           (unsaved-buffers-dialog (map url->string l) restart?)
           noop
           "Unsaved buffers")))))

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
  (list notify-enunciation-rendering notify-latex-command notify-look-and-feel notify-new-fonts notify-new-page-breaking notify-restart notify-scripting-language notify-security notify-tool))
