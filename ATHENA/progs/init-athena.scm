
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
                 (kernel athena tm-convert)
                 (kernel athena tm-language) (kernel athena tm-file-system)
                 (kernel athena tm-states))
(inherit-modules (kernel gui gui-markup)
                 (kernel gui menu-define) (kernel gui menu-widget)
                 (kernel gui kbd-define)
                 (kernel gui kbd-handlers))
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
(lazy-define (utils edit variants) make-inline-tag-list make-wrapped-tag-list)
(lazy-define (utils cas cas-out) cas->stree)

(lazy-define (utils test test-convert) delayed-quit
             build-manual build-ref-suite run-test-suite)
(import-from (utils library smart-table))

(import-from (utils misc markup-funcs))
(lazy-tmfs-handler (utils automate auto-tmfs) automate)
(lazy-define (utils automate auto-tmfs) auto-load-help)
(lazy-keyboard (utils automate auto-tmfs) in-auto?)
(lazy-keyboard (native-keyboard automate) in-auto?)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting main TeXmacs functionality\n")
(import-from (athena athena tm-server))
(lazy-define (athena athena tm-vault) load-vault-dir load-vault-dir-now)
(lazy-define (athena athena tm-global-transformation)
             run-global-transformation)
(lazy-define (athena athena tm-tools) clean-athena-cache)
(lazy-define (athena athena tm-view)
             toggle-full-screen-mode toggle-full-screen-edit-mode
             toggle-panorama-mode toggle-slideshow-mode
             toggle-remote-control-mode
             fit-to-screen fit-to-screen-width
             toggle-persistent-fit-width toggle-typewriter-mode
             toggle-snap-to-pages schedule-persistent-fit-width)
(lazy-define (athena athena tm-files)
             buffer-missing-style? buffer-set-default-style command-line-convert
             native-recent-file-provider-data
             native-import-format-provider-data native-export-format-provider-data
             native-selection-export-format-provider-data
             native-selection-import-format-provider-data
             native-selection-export-preference-provider-data
             native-selection-import-preference-provider-data)
(lazy-define (athena athena tm-codex)
             codex-ai-completion codex-ai-completion-new-buffer
             codex-ai-completion-custom)
(lazy-define (kernel athena tm-preferences) view-all-preferences)
(lazy-define (athena athena tm-preferences-ui)
             preferences-open? open-preferences)
(tm-define (notify-set-attachment name key val) (noop))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting generic mode\n")
(import-from (utils edit variants))
(import-from (utils library cursor))
(import-from (generic document-edit))
(import-from (generic generic-edit))
(import-from (generic format-drd))
(import-from (source source-edit))
(import-from (athena athena tm-files))
(import-from (athena athena tm-print))
(lazy-define (athena athena tm-print)
             wrapped-import-pdf-embeded-with-tm
             wrapped-print-to-pdf-embeded-with-tm)
(import-from (athena athena tm-vault))
(import-from (doc help-funcs))
(generic-keyboard-load)
(lazy-define (generic live-spell)
             spell-live-import-custom-dictionary-from-preferences)
(lazy-define (generic document-style)
             native-document-background-pattern-dialog
             native-document-background-gradient-dialog
             native-document-background-picture-dialog)
(lazy-define (generic document-part)
             buffer-has-preamble? in-preamble-mode? toggle-preamble-mode)
(lazy-define (generic document-edit) update-document set-document-language
             get-init-page-rendering init-page-rendering)
(lazy-define (athena athena tm-tools)
             show-character-count show-word-count show-line-count
             picture-gc toggle-save-aux toggle-show-kbd)
(lazy-define (athena athena tm-materials)
             insert-material-citation insert-referenced-materials
             materials-update-current-document materials-append-references
             materials-set-reference-style)
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
(lazy-define (generic generic-doc) focus-help)
(lazy-define (generic global-search) open-global-search
             global-search-open-result global-search-open-occurrence)
(lazy-define (generic format-widgets) open-paragraph-format open-page-format)
(lazy-define (generic document-widgets)
             open-document-paragraph-format open-document-page-format
             open-document-metadata)
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
(tm-property (open-document-paragraph-format) (:interactive #t))
(tm-property (open-document-page-format) (:interactive #t))
(tm-property (open-document-metadata) (:interactive #t))
(tm-property (open-pattern-selector cmd w) (:interactive #t))
(tm-property (open-gradient-selector cmd) (:interactive #t))
(tm-property (open-background-picture-selector cmd) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting text mode\n")
(lazy-keyboard (text text-edit) in-text?)
(lazy-keyboard (native-keyboard text) in-text?)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
(lazy-define (text text-drd) tm-register-new-list-tag)
(lazy-define (text text-edit)
             document-propose-title? document-propose-abstract?
             automatic-section-context? automatic-section-rename
             dueto-supporting-context? dueto-added? dueto-add
             make-doc-data make-abstract-data
             make-doc-data-element make-author-data-element
             make-abstract-data-element doc-data-has-hidden?
             doc-data-deactivated? doc-data-activate-toggle
             test-doc-title-clustering? set-doc-title-clustering
             previous-section section-context? native-section-switch-to)
(lazy-define (text text-structure) tm/section-get-title-string)

;(display "Booting math mode\n")
(lazy-keyboard (math math-sem-edit) in-sem-math?)
(lazy-define (math math-edit)
             brackets-refresh sqrt-toggle script-context? script-only-script?
             open-latex-formula-dialog)
(lazy-initialize (math math-edit) (in-math?))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting programming modes\n")
(lazy-format (prog code-format) cpp julia scala java json csv)
(lazy-format (prog python-format) python)
(lazy-keyboard (utils edit selections) in-prog?)
(lazy-keyboard (prog dot-edit) in-prog?)
(lazy-keyboard (prog java-edit) in-prog?)
(lazy-keyboard (prog scala-edit) in-prog?)
(lazy-keyboard (prog cpp-edit) in-prog?)
(lazy-keyboard (prog python-edit) in-prog?)
(lazy-keyboard (prog fortran-edit) in-prog?)
(lazy-keyboard (native-keyboard prog) in-prog?)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting source mode\n")
(lazy-keyboard (native-keyboard source) always?)
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
(lazy-keyboard (table table-edit) in-table?)
(lazy-keyboard (native-keyboard table) in-table?)
(lazy-define (table table-edit) table-resize-notify
             native-insert-small-table native-insert-big-table
             table-test-parwidth? table-toggle-parwidth)
(lazy-define (table table-widgets) open-cell-properties open-table-properties)
(tm-property (open-cell-properties) (:interactive #t))
(tm-property (open-table-properties) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting graphics mode\n")
(lazy-keyboard (graphics graphics-drd) in-graphics?)
(lazy-keyboard (graphics graphics-main) in-graphics?)
(lazy-keyboard (graphics graphics-utils) in-graphics?)
(lazy-keyboard (native-keyboard graphics) in-graphics?)
(lazy-define (graphics graphics-utils) make-graphics)
(lazy-define (graphics graphics-main) graphics-update-proviso
             graphics-get-proviso graphics-set-proviso)
(define-secure-symbols ext-fold-toc-in-reflow? toc-fold-tree toc-unfold-tree)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting formal and natural languages\n")
(lazy-define (kernel gui ui-text) replace)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting dynamic features\n")
(lazy-keyboard (dynamic fold-edit) always?)
(lazy-keyboard (native-keyboard fold) always?)
(lazy-define (dynamic fold-edit)
             screens-switch-to dynamic-make-slides overlays-context?
             overlay-context? overlays-current overlays-arity
             overlays-switch-to overlay-current overlay-arity overlay-visible?
             native-overlays-switch-parent
             beamer-themes current-beamer-theme
             slide-get-switch slide-get-document get-slide-name
             native-slide-insert-title native-slide-insert-graphics)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting documentation\n")
(lazy-keyboard (native-keyboard tmdoc) in-manual?)
(lazy-initialize (doc tmdoc-edit) (in-manual?))
(lazy-define (doc tmdoc-edit)
             tmdoc-make-branch tmdoc-insert-explain-synopsis
             tmdoc-propose-title? tmdoc-propose-copyright-and-license?
             tmdoc-insert-title tmdoc-insert-copyright-and-license)
(lazy-define (doc tmdoc) tmdoc-expand-help tmdoc-expand-help-manual
             tmdoc-expand-this tmdoc-include)
(lazy-define (doc docgrep) docgrep-in-doc docgrep-in-src
             docgrep-in-texts docgrep-in-recent)
(lazy-define (athena tools shortcut-listing) list-all-shortcuts)
(lazy-define (doc tmdoc-search) tmdoc-search-style tmdoc-search-tag
             tmdoc-search-parameter tmdoc-search-scheme)
(lazy-define (doc apidoc) apidoc-all-modules apidoc-all-symbols)
(lazy-tmfs-handler (doc docgrep) grep)
(lazy-tmfs-handler (doc tmdoc) help)
(lazy-tmfs-handler (doc apidoc) apidoc)
(define-secure-symbols tmdoc-include)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting converters\n")
(lazy-format (convert rewrite init-rewrite) texmacs verbatim)
(lazy-format (convert latex init-latex) latex)
(lazy-format (convert html init-html) html)
(lazy-format (convert markdown init-markdown) markdown)
(lazy-format (convert images init-images)
             postscript pdf xmgrace svg jpeg ppm gif png pnm)
(lazy-define (convert images tmimage)
             export-selection-as-graphics clipboard-copy-image
             native-export-selection-as-image-dialog)
(lazy-define (convert rewrite init-rewrite) texmacs->code texmacs->verbatim)
(lazy-define (convert html tmhtml) ext-tmhtml-eqnarray*)
(define-secure-symbols ext-tmhtml-eqnarray*)
(lazy-define (convert html tmhtml-expand) tmhtml-env-patch)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting linking facilities\n")
(lazy-define (link locus-edit) create-unique-id)
(lazy-define (link link-navigate) link-active-upwards link-active-ids
             link-follow-ids link-mouse-ids
             heading-word-count-schedule-refresh)
(lazy-define (link link-extern) get-constellation
             get-link-locations register-link-locations)
(lazy-define (link ref-edit) preview-reference)
(define-secure-symbols preview-reference)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting debugging facilities\n")
(lazy-define (debug debug-notifications) notify-debug-message
             acknowledge-debug-messages)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting editing modes for various special styles\n")
(lazy-define (various theme-edit) basic-themes current-basic-theme)
(lazy-define (various poster-edit)
             poster-themes poster-title-styles
             current-poster-theme current-poster-title-style
             poster-block-context? titled-block-context? block-wide?
             block-toggle-titled block-toggle-wide make-poster-title)
;(display* "time: " (- (texmacs-time) boot-start) "\n")


;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting fonts\n")
(lazy-define (fonts font-selector)
             open-font-selector open-document-font-selector
             open-document-other-font-selector)
(tm-property (open-font-selector) (:interactive #t))
(tm-property (open-document-font-selector) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting regression testing\n")
(lazy-define (check check-master) check-all run-checks run-all-tests)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "------------------------------------------------------\n")
(delayed (:idle 10000) (autosave-delayed))
(texmacs-banner)
;(display "Initialization done\n")
