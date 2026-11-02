
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
(initialize-secure-symbols)
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting utilities\n")

;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting main TeXmacs functionality\n")
(application-preferences-initialize)
(tm-define (notify-set-attachment name key val) (noop))
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting generic mode\n")
(initialize-node-groups)
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
(generic-keyboard-load)
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
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting formal and natural languages\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting dynamic features\n")
;(display* "time: " (- (texmacs-time) boot-start) "\n")

;(display "Booting linking facilities\n")
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
(native-dialogue-initialize)
(native-keyboard-initialize)
;(display "Initialization done\n")
