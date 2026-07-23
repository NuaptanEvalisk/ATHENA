
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : debug-menu.scm
;; DESCRIPTION : the debug menu
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (debug debug-menu))

(menu-bind debug-menu
  (-> "Execute"
      ("Execute system command" (interactive system))
      ("Evaluate scheme expression" (interactive footer-eval)))
  (-> "Consoles"
      ("Debugging console" (open-debug-console))
      ("Error messages" (error-messages-show)))
  (-> "Status"
      ("Tree" (show-tree))
      ("Box" (show-box))
      ("Path" (show-path))
      ("Cursors" (show-cursor))
      ("Selection" (show-selection))
      ("Focus" (display* "focus: " (get-focus-path) "\n"))
      ("Environment" (show-env))
      ("History" (show-history)))
  (-> "Timings"
      ("All" (bench-print-all)))
  (-> "Memory"
      ("Memory usage" (show-meminfo))
      ("Collect garbage" (gc)))
  (when (debug-get "correct")
    (-> "Mathematics"
        ("Error status report" (math-status-print))
        ("Reset error counters" (math-status-reset))))
  ---
  ("Test routine" (edit-test))
  ("Preferences" (open-preferences)))
