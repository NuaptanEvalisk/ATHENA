
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : developer-menu.scm
;; DESCRIPTION : Menu items for developer mode
;; COPYRIGHT   : (C) 2012 Miguel de Benito Delgado
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;; Things to do:
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus developer-menu))
(import-from (kernel athena tm-preferences))


(import-from (prog scheme-tools) (prog scheme-menu))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Miscellaneous extra routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (scm-load-buffer u)
   (load-document u) 
   (if (not (url-exists? u)) 
;; save empty file & reload so that it is recognized as scheme code, not plain tm doc  
      (begin (buffer-save u) (revert-buffer-revert))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The developer menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind provoke-error-menu
  (dynamic (gui-make '(xxx))))

(menu-bind developer-menu
  (group "Scheme")
  (link scheme-menu)
  ---
  (group "Crash Test")
  ("Trigger segfault" (cpp-error))
  ("Trigger Scheme error" (oops))
  (-> "Trigger menu error"
      (link provoke-error-menu))
  ---
  (group "Configuration")
  ((replace "Open %1" (verbatim "my-init-texmacs.scm"))
   (scm-load-buffer 
    (url-concretize "$ATHENA_HOME_PATH/progs/my-init-texmacs.scm")))
  ((replace "Open %1" (verbatim "my-init-buffer.scm"))
   (scm-load-buffer
    (url-concretize "$ATHENA_HOME_PATH/progs/my-init-buffer.scm")))
  ((replace "Open %1" (verbatim "preferences.json"))
   (scm-load-buffer
    (url-concretize "$ATHENA_HOME_PATH/system/preferences.json"))))
