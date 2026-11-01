
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
                 (kernel athena tm-document)
                 (kernel athena tm-language) (kernel athena tm-file-system)
                 (kernel athena tm-states))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting utilities\n")
(import-from (utils library cpp-wrap))
(import-from (utils base environment))
(lazy-define (utils library cursor) notify-cursor-moved cursor-history-add)
(lazy-define (utils automate auto-edit)
             make-block-if make-block-if-else make-block-for make-block-while
             make-block-assign make-block-intersperse make-block-tag
             make-inline-if make-inline-if-else make-inline-for make-inline-while
             make-inline-assign make-inline-intersperse make-inline-tag
             make-output-string make-inline-output make-block-output)
(lazy-define (utils cas cas-out) cas->stree)

(lazy-define (utils test test-convert) delayed-quit
             build-manual build-ref-suite run-test-suite)
(import-from (utils library smart-table))

(import-from (utils misc markup-funcs))
(lazy-tmfs-handler (utils automate auto-tmfs) automate)
(lazy-define (utils automate auto-tmfs) auto-load-help focus-help)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting main TeXmacs functionality\n")
(define-secure-symbols ext-render-exercises-smaller? ext-number-solutions?)
(application-preferences-initialize)
(register-preference-callback-procedures
  (list notify-header notify-icon-bar notify-toolbar-presentation
        notify-remote-control notify-status-bar notify-zoom-factor))
(lazy-define (athena athena tm-vault) load-vault-dir load-vault-dir-now)
(lazy-define (athena athena tm-files)
             buffer-missing-style? buffer-set-default-style command-line-convert)
(lazy-define (athena athena tm-codex)
             codex-ai-completion codex-ai-completion-new-buffer
             codex-ai-completion-custom)
(lazy-define (kernel athena tm-preferences) view-all-preferences)
(tm-define (notify-set-attachment name key val) (noop))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting generic mode\n")
(initialize-node-groups)
(import-from (utils edit variants))
(import-from (utils library cursor))
(import-from (generic document-edit))
(import-from (generic generic-edit))

(import-from (source source-edit))
(import-from (athena athena tm-files))
(import-from (athena athena tm-vault))
(import-from (doc help-funcs))
(generic-keyboard-load)
(lazy-define (generic document-part)
             buffer-has-preamble? in-preamble-mode? toggle-preamble-mode)
(lazy-define (generic document-edit) update-document set-document-language)
(tmfs-handler "material" 'load material-info-page)
(tmfs-handler "material" 'title materials-page-title)
(tmfs-handler "material" 'permission? materials-page-permission?)
(lazy-define (generic generic-edit) notify-activated notify-disactivated
             wheel-capture?
             native-insert-include-dialog
             native-insert-link-image-dialog
             native-insert-inline-image-dialog
             native-insert-thumbnails-dialog
             native-insert-small-figure
             native-insert-big-figure
             native-insert-floating-figure
             native-insert-floating-table
             native-insert-floating-algorithm)
(lazy-define (generic global-search) open-global-search
             global-search-open-result global-search-open-occurrence)
(lazy-define (generic format-widgets) open-paragraph-format open-page-format)
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
(lazy-define (source macro-edit)
             has-macro-source? edit-macro-source edit-focus-macro-source
             native-personal-macro-provider-data)
(lazy-define (source source-edit) extract-style-file)
(lazy-define (source shortcut-edit) init-user-shortcuts has-user-shortcut?)
(lazy-define (source shortcut-widgets) open-shortcuts-editor)
(tm-property (open-shortcuts-editor . opt) (:interactive #t))
(when (url-exists? "$ATHENA_HOME_PATH/system/shortcuts.scm")
  (delayed (:idle 100) (init-user-shortcuts)))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting table mode\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting graphics mode\n")
(define-secure-symbols ext-fold-toc-in-reflow? toc-fold-tree toc-unfold-tree)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting formal and natural languages\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting dynamic features\n")
(lazy-define (dynamic fold-edit)
             screens-switch-to dynamic-make-slides overlays-context?
             overlay-context? overlays-current overlays-arity
             overlays-switch-to overlay-current overlay-arity overlay-visible?
             native-overlays-switch-parent
             slide-get-switch slide-get-document get-slide-name
             native-slide-insert-title native-slide-insert-graphics)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting documentation\n")
(lazy-initialize (doc tmdoc-edit) (in-manual?))
(lazy-define (doc tmdoc-edit)
             tmdoc-make-branch tmdoc-insert-explain-synopsis
             tmdoc-propose-title? tmdoc-propose-copyright-and-license?
             tmdoc-insert-title tmdoc-insert-copyright-and-license)
(lazy-define (doc tmdoc) tmdoc-expand-help tmdoc-expand-help-manual
             tmdoc-expand-this tmdoc-include)
(lazy-define (doc tmdoc-search) tmdoc-search-style tmdoc-search-tag
             tmdoc-search-parameter tmdoc-search-scheme)
(lazy-tmfs-handler (doc tmdoc) help)
(define-secure-symbols tmdoc-include)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting linking facilities\n")
(lazy-define (link link-navigate) link-follow-ids)
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
