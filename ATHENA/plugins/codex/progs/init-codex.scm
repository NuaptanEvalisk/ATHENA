;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-codex.scm
;; DESCRIPTION : OpenAI Codex AppServer session
;; COPYRIGHT   : (C) 2026 Nuaptan Felix Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (codex-home-path)
  (let ((configured (get-preference "codex home")))
    (if (== configured "")
        (string-append (getenv "ATHENA_HOME_PATH") "/codex")
        configured)))

(define (codex-bridge-path)
  (if (url-exists? "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize
        (url-resolve-in-path "athena-codex-bridge"))))

(define (codex-session-launcher)
  (string-append (escape-shell (codex-bridge-path))
                 " --codex-home "
                 (escape-shell (codex-home-path))))

(define (codex-serialize lan t)
  (when (tm-func? t 'document 1) (set! t (tm-ref t 0)))
  (string-append
    (if (tm-atomic? t)
        (cork->utf8 (tm->string t))
        (convert (tm->stree t) "texmacs-stree" "latex-snippet"
                 (cons "texmacs->latex:encoding" "utf-8")))
    "\n"))

(plugin-configure codex
  (:require (and (or (url-exists? "$ATHENA_PATH/bin/athena-codex-bridge")
                     (url-exists-in-path? "athena-codex-bridge"))
                 (or (url-exists-in-path? "codex")
                     (url-exists? "$ATHENA_PATH/bin/codex"))))
  (:launch ,(codex-session-launcher))
  (:serializer ,codex-serialize)
  (:session "ChatGPT"))
