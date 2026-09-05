
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

(define developer-mode?
  (equal? (cpp-get-preference "developer tool" "off") "on"))

(if developer-mode?
    (if (equal? (scheme-dialect) "guile-d")
        (debug-enable 'backtrace)
        (debug-enable 'backtrace 'debug)))

(define (%new-read-hook sym) (noop)) ; for autocompletion

(define-public macro-keywords '(define-macro define-public-macro 
                                tm-define-macro))
(define-public def-keywords
  `(define-public provide-public
    tm-define tm-menu menu-bind tm-widget ,@macro-keywords))

(define tm-interactive-hook tm-interactive)

(define old-read read)
(define (new-read port)
  "A redefined reader which stores line number and file name in symbols."
  ;; FIXME: handle overloaded definitions
  (let ((form (old-read port)))
    (if (and (pair? form) (member (car form) def-keywords))
        (let* ((l (source-property form 'line))
               (c (source-property form 'column))
               (f (source-property form 'filename))
               (sym  (if (pair? (cadr form)) (caadr form) (cadr form))))
          (if (symbol? sym) ; Just in case
              (let ((old (or (symbol-property sym 'defs) '()))
                    (new `(,f ,l ,c)))
                (%new-read-hook sym)
                (if (and (member (car form) macro-keywords)
                         (not (member sym def-keywords)))
                    (set! def-keywords (cons sym def-keywords)))
                (if (not (member new old))
                    (set-symbol-property! sym 'defs (cons new old)))))))
    form))

(define old-primitive-load primitive-load)
(define startup-load-profile? (equal? (getenv "ATHENA_STARTUP_PROFILE") "1"))

(define (startup-profiled-primitive-load filename)
  (let* ((start (texmacs-time))
         (result (old-primitive-load filename))
         (elapsed (- (texmacs-time) start)))
    (display "ATHENA-STARTUP-LOAD\t")
    (display elapsed)
    (display "\t")
    (display filename)
    (newline)
    result))

(if startup-load-profile?
    (set! primitive-load startup-profiled-primitive-load))

(define (new-primitive-load filename)
  (if (member (scheme-dialect) (list "guile-a" "guile-b"))
      (old-primitive-load filename)
      ;; We explicitly circumvent guile's decision to set the current-reader
      ;; to #f inside ice-9/boot-9.scm, try-module-autoload
      (with-fluids ((current-reader read))
                   (old-primitive-load filename))))

(if developer-mode?
    (begin
      (module-export! (current-module)
                      '(%new-read-hook old-read new-read def-keywords))
      (set! read new-read)
      (module-export! (current-module)
                      '(old-primitive-load new-primitive-load))
      (set! primitive-load new-primitive-load)))

;; TODO: scheme file caching using (set! primitive-load ...) and
;; (set! %search-load-path)

;;(debug-enable 'backtrace 'debug)
;; (define load-indent 0)
;; (define old-primitive-load primitive-load)
;; (define (new-primitive-load . x)
;;   (for-each display (make-list load-indent "  "))
;;   (display "Load ") (apply display x) (display "\n")
;;   (set! load-indent (+ load-indent 1))
;;   (apply old-primitive-load x)
;;   (set! load-indent (- load-indent 1))
;;   (for-each display (make-list load-indent "  "))
;;   (display "Done\n"))
;; (set! primitive-load new-primitive-load)

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
                 (kernel athena tm-plugins) (kernel athena tm-secure)
                 (kernel athena tm-convert)
                 (kernel athena tm-language) (kernel athena tm-file-system)
                 (kernel athena tm-states))
(inherit-modules (kernel gui gui-markup)
                 (kernel gui menu-define) (kernel gui menu-widget)
                 (kernel gui kbd-define)
                 (kernel gui kbd-handlers)
                 (kernel gui menu-test)
                 (kernel old-gui old-gui-widget)
                 (kernel old-gui old-gui-factory)
                 (kernel old-gui old-gui-form)
                 (kernel old-gui old-gui-test))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting utilities\n")
(import-from (utils library cpp-wrap))
(lazy-define (utils library cursor) notify-cursor-moved)
(lazy-define (utils edit variants) make-inline-tag-list make-wrapped-tag-list)
(lazy-define (utils cas cas-out) cas->stree)
(lazy-define (utils plugins plugin-cmd) pre-serialize verbatim-serialize)
(lazy-define (utils test test-convert) delayed-quit
             build-manual build-ref-suite run-test-suite)
(import-from (utils library smart-table))
(when (not (qt-gui?))
  (use-modules (utils plugins plugin-convert)))
(import-from (utils misc markup-funcs))
(import-from (utils misc artwork))
(lazy-define (utils handwriting handwriting) learn-glyphs)
(lazy-tmfs-handler (utils automate auto-tmfs) automate)
(lazy-tmfs-handler (athena athena tm-vault-welcome) welcome)
(lazy-define (athena athena tm-vault-welcome)
             go-to-system-welcome-page go-to-welcome-page
             go-to-vault-initial-page)
(lazy-define (utils automate auto-tmfs) auto-load-help)
(lazy-define (utils misc gui-keyboard) get-keyboard)
(lazy-keyboard (utils automate auto-kbd) in-auto?)
(define supports-email? (url-exists-in-path? "mmail"))
(if supports-email? (use-modules (utils email email-tmfs)))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting main TeXmacs functionality\n")
(import-from (athena athena tm-server) (athena athena tm-vault-startup))
(lazy-define (athena athena tm-vault) load-vault-dir)
(lazy-define (athena athena tm-global-transformation)
             run-global-transformation)
(lazy-define (athena athena tm-files)
             buffer-missing-style? buffer-set-default-style command-line-convert)
(import-from (athena keyboard config-kbd))
(lazy-keyboard (athena keyboard prefix-kbd) always?)
(lazy-keyboard (athena keyboard latex-kbd) always?)
(lazy-menu (athena menus file-menu)
           file-menu go-menu buffer-go-menu
           new-file-menu load-menu save-menu
           print-menu print-menu-inline close-menu)
(lazy-menu (athena menus edit-menu) edit-menu)
(lazy-menu (athena menus view-menu) view-menu texmacs-bottom-toolbars)
(lazy-menu (athena menus interface-menu) interface-menu)
(lazy-menu (athena menus utility-menus)
           athena-go-utilities-menu
           athena-view-panes-menu
           athena-workspace-utilities-menu
           athena-file-utilities-menu
           athena-document-utilities-menu
           athena-edit-utilities-menu
           athena-interface-utilities-menu
           athena-help-utilities-menu)
(lazy-menu (athena menus preferences-widgets)
           preferences-open?
           open-preferences open-plugin-preferences open-plugins-preferences)
(lazy-menu (athena menus main-menu)
           texmacs-extra-menu texmacs-extra-icons
           plugin-menu plugin-icons bookmarks-menu test-menu help-icons
           athena-focus-menu texmacs-menu window-list-menu
           workspace-menu presentation-popup-menu texmacs-popup-menu
           texmacs-alternative-popup-menu texmacs-main-icons
           texmacs-mode-icons)
(lazy-define (athena menus file-menu) recent-file-list recent-directory-list)
(lazy-define (athena menus view-menu) set-bottom-bar test-bottom-bar?)
(lazy-tool (athena menus view-tools) retina-settings-tool)
(tm-define (notify-set-attachment name key val) (noop))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting generic mode\n")
(lazy-keyboard (generic generic-kbd) always?)
(lazy-keyboard (generic live-spell) always?)
(lazy-menu (generic live-spell) spell-live-popup-menu)
(lazy-define (generic live-spell)
             spell-live-import-custom-dictionary-from-preferences)
(lazy-menu (generic generic-menu) focus-menu texmacs-focus-icons)
(lazy-menu (generic format-menu) format-menu
           font-size-menu color-menu horizontal-space-menu
           transform-menu specific-menu
           text-font-effects-menu text-effects-menu
           vertical-space-menu indentation-menu line-break-menu
           page-header-menu page-footer-menu page-numbering-menu
           page-break-menu)
(lazy-menu (generic document-menu) document-menu
           document-style-menu)
(lazy-menu (generic document-part)
           preamble-menu)
(lazy-define (generic document-part)
             buffer-has-preamble? in-preamble-mode? toggle-preamble-mode)
(lazy-menu (generic insert-menu) insert-menu texmacs-insert-menu
           texmacs-insert-icons insert-link-menu insert-image-menu)
(lazy-define (generic document-edit) update-document set-document-language
             get-init-page-rendering init-page-rendering)
(lazy-define (generic generic-edit) notify-activated notify-disactivated
             wheel-capture?)
(lazy-define (generic generic-doc) focus-help)
(lazy-define (generic search-widgets) replace-toolbar
             open-replace toolbar-replace-start interactive-replace
             search-next-match open-global-search)
(lazy-define (generic spell-widgets) spell-toolbar
             open-spell toolbar-spell-start interactive-spell)
(lazy-define (generic format-widgets) open-paragraph-format open-page-format)
(lazy-define (generic pattern-selector) open-pattern-selector
             open-gradient-selector open-background-picture-selector)
(lazy-define (generic document-widgets)
             open-document-paragraph-format open-document-page-format
             open-document-metadata open-document-colors)
(lazy-tool (generic format-tools)
           format-paragraph-tool format-page-tool
           document-page-tool
           sections-tool subsections-tool)
(lazy-tool (generic document-tools)
           document-colors-tool)
(lazy-tool (generic pattern-tools)
           color-tool pattern-tool gradient-tool picture-tool)
(tm-property (open-replace) (:interactive #t))
(tm-property (open-paragraph-format) (:interactive #t))
(tm-property (open-page-format) (:interactive #t)
                                (:applicable (not (selection-active?))))
(tm-property (open-document-paragraph-format) (:interactive #t))
(tm-property (open-document-page-format) (:interactive #t))
(tm-property (open-document-metadata) (:interactive #t))
(tm-property (open-document-colors) (:interactive #t))
(tm-property (open-pattern-selector cmd w) (:interactive #t))
(tm-property (open-gradient-selector cmd) (:interactive #t))
(tm-property (open-background-picture-selector cmd) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting text mode\n")
(lazy-keyboard (text text-kbd) in-text?)
(lazy-keyboard (text chinese chinese) in-chinese?)
(lazy-menu (text text-menu) text-format-menu text-format-icons
	   text-menu text-block-menu text-inline-menu
           text-icons text-block-icons text-inline-icons)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")
(lazy-define (text text-drd) tm-register-new-list-tag)

;(display "Booting math mode\n")
(lazy-keyboard (math math-kbd) in-math?)
(lazy-keyboard (math math-sem-edit) in-sem-math?)
(lazy-menu (math math-menu) math-format-menu math-format-icons
	   math-menu math-insert-menu
           math-icons math-insert-icons
           math-correct-menu semantic-math-preferences-menu
           context-preferences-menu insert-math-menu)
(lazy-initialize (math math-menu) (in-math?))
(lazy-define (math math-edit) brackets-refresh)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting programming modes\n")
(lazy-format (prog prog-format) scheme)
(lazy-format (code-format) cpp julia scala java json csv)
(lazy-format (python-format) python)
(lazy-keyboard (prog prog-kbd) in-prog?)
(lazy-menu (prog prog-menu) prog-format-menu prog-format-icons
	   prog-menu prog-icons)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting source mode\n")
(lazy-keyboard (source source-kbd) always?)
(lazy-menu (source source-menu) source-macros-menu source-menu source-icons
           source-transformational-menu source-executable-menu)
(lazy-define (source macro-edit)
             has-macro-source? edit-macro-source edit-focus-macro-source)
(lazy-menu (source macro-menu) insert-macro-menu)
(lazy-define (source macro-widgets)
             editable-macro? open-macros-editor
	     open-macro-editor create-table-macro
             edit-focus-macro edit-previous-macro)
(lazy-define (source shortcut-edit) init-user-shortcuts has-user-shortcut?)
(lazy-define (source shortcut-widgets) open-shortcuts-editor)
(tm-property (open-macro-editor l mode) (:interactive #t))
(tm-property (create-table-macro l mode) (:interactive #t))
(tm-property (open-macros-editor mode) (:interactive #t))
(tm-property (edit-focus-macro) (:interactive #t))
(tm-property (open-shortcuts-editor . opt) (:interactive #t))
(when (url-exists? "$ATHENA_HOME_PATH/system/shortcuts.scm")
  (delayed (:idle 100) (init-user-shortcuts)))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting table mode\n")
(lazy-keyboard (table table-kbd) in-table?)
(lazy-menu (table table-menu) insert-table-menu)
(lazy-define (table table-edit) table-resize-notify)
(lazy-define (table table-widgets) open-cell-properties open-table-properties)
(lazy-tool (table table-tools) cell-properties-tool table-properties-tool)
(tm-property (open-cell-properties) (:interactive #t))
(tm-property (open-table-properties) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting graphics mode\n")
(lazy-keyboard (graphics graphics-kbd) in-active-graphics? graphics-wheel)
(lazy-menu (graphics graphics-menu) graphics-menu graphics-icons
           graphics-focus-icons)
(lazy-define (graphics graphics-object)
             graphics-reset-state graphics-decorations-update)
(lazy-define (graphics graphics-utils) make-graphics)
(lazy-define (graphics graphics-edit)
             graphics-busy?
             graphics-reset-context graphics-undo-enabled
             graphics-release-left graphics-release-middle
             graphics-release-right graphics-start-drag-left
             graphics-dragging-left graphics-end-drag-left)
(lazy-define (graphics graphics-main) graphics-update-proviso
             graphics-get-proviso graphics-set-proviso)
(lazy-define (graphics graphics-markup) arrow-with-text arrow-with-text*)
(define-secure-symbols arrow-with-text arrow-with-text*)
(lazy-define (athena athena commutative-diagram)
             make-cd in-commutative-diagram? commutative-diagram-layout
             commutative-diagram-handle commutative-diagram-context-menu?
             commutative-diagram-show-hidden commutative-diagram-describe)
(lazy-keyboard (athena athena commutative-diagram)
               commutative-diagram-keyboard?)
(lazy-menu (athena athena commutative-diagram)
           commutative-diagram-popup-menu commutative-diagram-focus-menu
           commutative-diagram-focus-icons)
(define-secure-symbols commutative-diagram-layout
  commutative-diagram-handle)
(define-secure-symbols ext-fold-toc-in-reflow? toc-fold-tree toc-unfold-tree)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting formal and natural languages\n")
(lazy-language (language minimal) minimal)
(lazy-language (language std-math) std-math)
(lazy-define (kernel gui ui-text) replace)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting educational features\n")
(lazy-keyboard (education edu-kbd) in-edu-text?)
(lazy-menu (education edu-menu) edu-insert-menu)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting dynamic features\n")
(lazy-keyboard (dynamic fold-kbd) always?)
(lazy-keyboard (dynamic scripts-kbd) always?)
(lazy-keyboard (dynamic calc-kbd) always?)
(lazy-menu (dynamic fold-menu) insert-fold-menu dynamic-menu dynamic-icons
           graphics-overlays-menu graphics-screens-menu
           graphics-focus-overlays-menu graphics-focus-overlays-icons)
(lazy-menu (dynamic session-menu) insert-session-menu session-help-icons)
(lazy-menu (dynamic scripts-menu) scripts-eval-menu scripts-plot-menu
           plugin-eval-menu plugin-eval-toggle-menu plugin-plot-menu)
(lazy-menu (dynamic calc-menu) calc-table-menu calc-insert-menu
           calc-icourse-menu)
(lazy-menu (dynamic animate-menu) insert-animation-menu animate-toolbar)
(lazy-define (dynamic fold-edit)
             screens-switch-to dynamic-make-slides overlays-context?)
(lazy-define (dynamic session-edit) scheme-eval)
(lazy-define (dynamic calc-edit) calc-ready? calc-table-renumber)
(lazy-define (dynamic scripts-plot) open-plots-editor)
(lazy-initialize (dynamic session-menu) (in-session?))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting documentation\n")
(lazy-keyboard (doc tmdoc-kbd) in-manual?)
(lazy-keyboard (doc apidoc-kbd) developer-mode?)
(lazy-menu (doc tmdoc-menu) tmdoc-menu tmdoc-icons)
(lazy-menu (doc help-menu) help-menu)
(lazy-define (doc tmdoc) tmdoc-expand-help tmdoc-expand-help-manual
             tmdoc-expand-this tmdoc-include)
(lazy-define (doc docgrep) docgrep-in-doc docgrep-in-src
             docgrep-in-texts docgrep-in-recent)
(lazy-define (doc tmdoc-search) tmdoc-search-style tmdoc-search-tag
             tmdoc-search-parameter tmdoc-search-scheme)
(lazy-define (doc apidoc) apidoc-all-modules apidoc-all-symbols)
(lazy-menu (doc apidoc-menu) apidoc-menu)
(lazy-tmfs-handler (doc docgrep) grep)
(lazy-tmfs-handler (doc tmdoc) help)
(lazy-tmfs-handler (doc apidoc) apidoc)
(define-secure-symbols tmdoc-include)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting converters\n")
(lazy-format (convert rewrite init-rewrite) texmacs verbatim)
(lazy-format (convert tmml init-tmml) tmml)
(lazy-format (convert latex init-latex) latex)
(lazy-format (convert html init-html) html)
(lazy-format (convert markdown init-markdown) markdown)
(lazy-format (convert images init-images)
             postscript pdf xmgrace svg xpm jpeg ppm gif png pnm)
(lazy-define (convert images tmimage)
             export-selection-as-graphics clipboard-copy-image)
(lazy-define (convert rewrite init-rewrite) texmacs->code texmacs->verbatim)
(lazy-define (convert html tmhtml) ext-tmhtml-eqnarray*)
(define-secure-symbols ext-tmhtml-eqnarray*)
(lazy-define (convert html tmhtml-expand) tmhtml-env-patch)
(lazy-define (convert latex latex-drd) latex-arity latex-type)
(lazy-define (convert latex tmtex) tmtex-env-patch)
(lazy-define (convert latex latex-tools) latex-set-virtual-packages
             latex-has-style? latex-has-package?
             latex-has-texmacs-style? latex-has-texmacs-package?)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting database facilities\n")
(lazy-define (database db-widgets) open-db-chooser)
(lazy-define (database db-menu) db-show-toolbar)
(lazy-define (database db-convert) db-url?)
(lazy-menu (database db-menu) db-menu db-toolbar)
(lazy-tmfs-handler (database db-tmfs) db)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting linking facilities\n")
(lazy-menu (link link-menu) link-menu)
(lazy-keyboard (link link-kbd) with-linking-tool?)
(lazy-define (link link-edit) create-unique-id)
(lazy-define (link link-navigate) link-active-upwards link-active-ids
             link-follow-ids link-mouse-ids
             heading-word-count-schedule-refresh)
(lazy-define (link link-extern) get-constellation
             get-link-locations register-link-locations)
(lazy-define (link ref-edit) preview-reference)
(define-secure-symbols preview-reference)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting debugging and developer facilities\n")
(lazy-menu (debug debug-menu) debug-menu)
(lazy-menu (athena menus developer-menu)
           developer-menu custom-keyboard-toolbar)
(lazy-define (debug debug-widgets) notify-debug-message
             acknowledge-debug-messages
             open-debug-console open-error-messages)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting editing modes for various special styles\n")
(lazy-menu (various poster-menu) poster-block-menu)
(lazy-menu (various theme-menu) basic-theme-menu)
(lazy-define (various theme-edit) current-basic-theme)
(lazy-define (various theme-menu) basic-theme-name)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting plugins\n")
(for-each lazy-plugin-initialize (plugin-list))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting fonts\n")
(import-from (fonts fonts-ec) (fonts fonts-adobe) (fonts fonts-x)
             (fonts fonts-math) (fonts fonts-foreign) (fonts fonts-misc)
             (fonts fonts-composite))
(lazy-define (fonts font-old-menu)
	     text-font-menu math-font-menu prog-font-menu)
(lazy-define (fonts font-new-widgets)
             open-font-selector open-document-font-selector
             open-document-other-font-selector)
(tm-property (open-font-selector) (:interactive #t))
(tm-property (open-document-font-selector) (:interactive #t))
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "Booting regression testing\n")
(lazy-define (check check-master) check-all run-checks run-all-tests)
;(display* "time: " (- (texmacs-time) boot-start) "\n")
;(display* "memory: " (texmacs-memory) " bytes\n")

;(display "------------------------------------------------------\n")
(delayed (:idle 10000) (autosave-delayed))
(texmacs-banner)
;(display "Initialization done\n")
