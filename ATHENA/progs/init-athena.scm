
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-athena.scm
;; DESCRIPTION : This is the standard ATHENA initialization file
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(cond ((os-mingw?)
       (debug-set! stack 0))
      ((os-macos?)
       (debug-set! stack 2000000))
      (else
       (debug-set! stack 1000000)))

(define boot-start (texmacs-time))

(define tm-interactive-hook tm-interactive)
(define base-primitive-load primitive-load)
(define startup-load-profile? (equal? (getenv "ATHENA_STARTUP_PROFILE") "1"))

(define (startup-profiled-primitive-load filename)
  (let* ((start (texmacs-time))
         (result (base-primitive-load filename))
         (elapsed (- (texmacs-time) start)))
    (display "ATHENA-STARTUP-LOAD\t")
    (display elapsed)
    (display "\t")
    (display filename)
    (newline)
    result))

(if startup-load-profile?
    (set! primitive-load startup-profiled-primitive-load))

;(display "Booting TeXmacs kernel functionality\n")
(primitive-load (url-concretize "$ATHENA_PATH/progs/kernel/boot/boot.scm"))
(inherit-modules (kernel boot compat) (kernel boot abbrevs)
                 (kernel boot debug) (kernel boot srfi)
                 (kernel boot ahash-table) (kernel boot prologue))
(inherit-modules (kernel library base) (kernel library list)
                 (kernel library tree) (kernel library content)
                 (kernel library patch))
(inherit-modules (kernel regexp regexp-match) (kernel regexp regexp-select))
(inherit-modules (kernel logic logic-rules) (kernel logic logic-query)
                 (kernel logic logic-data))
(inherit-modules (kernel athena tm-define)
                 (kernel athena tm-dialogue)
                 (kernel athena tm-preferences) (kernel athena tm-modes)
                 (kernel athena tm-secure)
                 (kernel athena tm-file-system))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting utilities\n")
(import-from (utils library cpp-wrap))
(lazy-define (utils library cursor) notify-cursor-moved cursor-history-add)

(import-from (utils misc markup-funcs))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting main TeXmacs functionality\n")
(define-secure-symbols ext-render-exercises-smaller? ext-number-solutions?)
(application-preferences-initialize)
(register-preference-callback-procedures
  (list notify-header notify-icon-bar notify-toolbar-presentation
        notify-remote-control notify-status-bar notify-zoom-factor))
(lazy-define (kernel athena tm-preferences) view-all-preferences)
(tm-define (notify-set-attachment name key val) (noop))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting generic mode\n")
(initialize-node-groups)
(import-from (utils edit variants))
(import-from (utils library cursor))
(import-from (utils edit selections))
(import-from (utils misc tooltip))
(define-secure-symbols doc-data author-data abstract-data ext-edit-macro
  screens-index screens-arity screens-bar screens-switch-to
  ext-cardlink-render system-icon-for-link cardlink-native-type cardlink-native-render
  display-balloon)
(register-preference-callback-procedures (list notify-autosave))
(tm-property (choose-file fun text type) (:interactive #t))
(tm-property (load-buffer name . opts)
  (:argument name smart-file "File name")
  (:default name (propose-name-buffer)))
(tm-property (load-buffer-in-new-window name . opts)
  (:argument name smart-file "File name")
  (:default name (propose-name-buffer)))
(tm-property (load-document u)
  (:argument u smart-file "File name")
  (:default u (propose-name-buffer)))
(tm-property (load-document* u)
  (:argument u smart-file "File name")
  (:default u (propose-name-buffer)))
(tm-property (switch-document u)
  (:argument u smart-file "File name")
  (:default u (propose-name-buffer)))
;; Registration bridges for the native vault frontend; kernel TMFS and
;; preference registries retain their existing initialization order.
(define-secure-symbols
  wikilink-repair-apply vault-transclude-repair
  node-reference-jump-to-source vault-jump-to-source
  artifact-jump-to-position artifact-navigation-failed
  load-buffer load-vault-dir string->url vault-load-latest-action
  vault-validate-root-namespace go-to-system-welcome-page
  go-to-welcome-page go-to-vault-initial-page
  vault-show-explorer vault-show-explorer-and-track vault-explorer-track-file
  open-namespace-manager open-namespace-explorer
  namespace-new-file-within-wizard namespace-info-page open-quick-switcher
  new-document)
(tmfs-handler "wikilink-repair" 'load wikilink-repair-page)
(tmfs-handler "ns" 'load namespace-info-page)
(tmfs-load-handler (welcome name) (vault-welcome-page))
(tmfs-handler "artifact" 'load artifact-open-page)
(tmfs-title-handler (artifact name doc) "Artifact")
(tmfs-permission-handler (artifact name kind) (== kind "read"))
(tmfs-handler "artifact-disambiguation" 'load artifact-disambiguation-page)
(tmfs-title-handler (artifact-disambiguation name doc) "Artifact disambiguation")
(tmfs-permission-handler (artifact-disambiguation name kind) (== kind "read"))
(tmfs-handler "artifact-name-query" 'load artifact-name-query-page)
(tmfs-title-handler (artifact-name-query name doc) "Artifact disambiguation")
(tmfs-permission-handler (artifact-name-query name kind) (== kind "read"))
(register-preference-callback-procedures
  (list notify-cursor-color notify-document-background-color
        notify-enunciation-color notify-focus-border-width notify-focus-color
        notify-labels-mode notify-link-color notify-selection-color
        notify-vault-explorer-track notify-vault-preferences-mode))
(generic-keyboard-load)
(tmfs-handler "material" 'load material-info-page)
(tmfs-handler "material" 'title materials-page-title)
(tmfs-handler "material" 'permission? materials-page-permission?)
(tm-property (open-replace) (:interactive #t))
(tm-property (make-with var val) (:check-mark "o" test-env?))
(tm-property (make-line-with var val)
  (:synopsis "Make 'with' with one or more paragraphs as its scope")
  (:check-mark "o" test-env?))
(tm-property (make-interactive-with var) (:interactive #t))
(tm-property (make-interactive-line-with var) (:interactive #t))
(tm-property (make-interactive-with-opacity) (:interactive #t))
(tm-property (make-alternate prompt default-val tag) (:interactive #t))
(tm-property (make-hspace spc)
  (:synopsis "Insert stretchable space")
  (:argument spc "Horizontal space"))
(tm-property (make-space spc)
  (:synopsis "Insert rigid space")
  (:argument spc "Horizontal space"))
(tm-property (make-var-space spc base top)
  (:synopsis "Insert rigid space")
  (:argument spc "Horizontal space")
  (:argument base "Base level")
  (:argument top "Top level"))
(tm-property (make-htab spc)
  (:synopsis "Insert horizontal tab")
  (:argument spc "Minimal space"))
(tm-property (make-vspace-before spc)
  (:synopsis "Insert space before")
  (:argument spc "Vertical space"))
(tm-property (make-vspace-after spc)
  (:synopsis "Insert space after")
  (:argument spc "Vertical space"))
(tm-property (set-effect-pen t pen) (:check-mark "*" test-effect-pen?))
(tm-property (make-move hor ver)
  (:argument hor "Horizontal") (:argument ver "Vertical"))
(tm-property (make-shift hor ver)
  (:argument hor "Horizontal") (:argument ver "Vertical"))
(tm-property (make-resize l b r t)
  (:argument l "Left") (:argument b "Bottom")
  (:argument r "Right") (:argument t "Top"))
(tm-property (make-extend l b r t)
  (:argument l "Left") (:argument b "Bottom")
  (:argument r "Right") (:argument t "Top"))
(tm-property (make-clipped l b r t)
  (:argument l "Left") (:argument b "Bottom")
  (:argument r "Right") (:argument t "Top"))
(tm-property (make-reduce-by by) (:argument by "Reduce by"))
(tm-property (open-paragraph-format) (:interactive #t))
(tm-property (open-page-format) (:interactive #t)
                                (:applicable (not (selection-active?))))
(tm-property (open-pattern-selector cmd w) (:interactive #t))
(tm-property (open-gradient-selector cmd) (:interactive #t))
(tm-property (open-background-picture-selector cmd) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting text mode\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")
(define-secure-symbols mouse-toggle-todo-item mouse-toggle-cloud-todo-item)

;(display "Booting math mode\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting programming modes\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting source mode\n")
(tm-property (open-shortcuts-editor . opt) (:interactive #t))
(init-user-shortcuts)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting table mode\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting graphics mode\n")
(define-secure-symbols ext-fold-toc-in-reflow? toc-fold-tree toc-unfold-tree)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting formal and natural languages\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting dynamic features\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting linking facilities\n")
(define-secure-symbols preview-reference)
(define-secure-symbols reverse-hierarchy-graph-render global-hierarchy-graph-show
                       websites-manager-show open-websites-manager
                       ext-typed-ref* ext-typed-ref ext-smart-ref)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting debugging facilities\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting editing modes for various special styles\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")


;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting fonts\n")
(tm-property (open-font-selector) (:interactive #t))
(tm-property (open-document-font-selector) (:interactive #t))
(tm-property (open-document-other-font-selector prefix) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "------------------------------------------------------\n")
(delayed (:idle 10000) (autosave-delayed))
(native-keyboard-initialize)
(texmacs-banner)
;(display "Initialization done\n")
