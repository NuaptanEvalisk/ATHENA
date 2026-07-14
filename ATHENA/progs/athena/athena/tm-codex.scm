;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-codex.scm
;; DESCRIPTION : Codex-backed ATHENA document commands
;; COPYRIGHT   : (C) 2026 Nuaptan Felix Evalisk
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-codex)
  (:use (utils library cursor)
        (utils library tree)))

(define (codex-home-path)
  (let ((configured (get-preference "codex home")))
    (if (== configured "")
        (string-append (getenv "ATHENA_HOME_PATH") "/codex")
        configured)))

(define (codex-completion-prompt latex)
  (string-append
    "Continue the following ATHENA document selection. It was exported as "
    "a LaTeX snippet. Return only the LaTeX snippet that should be inserted "
    "immediately after it: no Markdown fence, explanation, or repetition of "
    "the supplied text. Preserve its language, mathematical notation, style, "
    "and level of formality.\n\n"
    latex))

(define (codex-bridge-path)
  (if (url-exists? "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize
        (url-resolve-in-path "athena-codex-bridge"))))

(define (codex-remove-completion-files input output)
  (when (url-exists? input) (url-remove input))
  (when (url-exists? output) (url-remove output)))

(define (codex-replace-placeholder buffer placeholder replacement)
  (and (buffer-exists? buffer)
       (tree->path placeholder)
       (with-buffer buffer
         (let ((saved-position
                 (and (not (tree-inside? (cursor-tree) placeholder))
                      (position-new))))
           (when saved-position
             (position-set saved-position (cursor-path)))
           (tree-go-to placeholder :start)
           (let ((insertion-position (position-new)))
             (position-set insertion-position (cursor-path))
             (tree-assign! placeholder "")
             (go-to (position-get insertion-position))
             (insert replacement)
             (position-delete insertion-position))
           (when saved-position
             (go-to (position-get saved-position))
             (position-delete saved-position))
           (update-current-buffer)))))

(define (codex-finish-completion buffer placeholder input output)
  (if (url-exists? output)
      (let* ((answer (string-load output))
             (converted (convert answer "latex-snippet" "texmacs-stree")))
        (if (!= answer "")
            (codex-replace-placeholder
              buffer placeholder (stree->tree converted))
            (when (== answer "")
              (codex-replace-placeholder
                buffer placeholder
                '(with "color" "red" "Codex returned no completion"))
              (set-message "Codex returned an empty completion"
                           "AI completion"))))
      (begin
        (codex-replace-placeholder
          buffer placeholder
          '(with "color" "red" "Codex completion failed"))
        (set-message "Codex could not generate a completion"
                     "AI completion")))
  (codex-remove-completion-files input output))

(tm-define (codex-ai-completion)
  (if (not (selection-active-any?))
      (set-message "Select text to continue with Codex" "AI completion")
      (let* ((selection (selection-tree))
             (end (selection-get-end))
             (latex (convert (tm->stree selection)
                             "texmacs-stree" "latex-snippet"
                             (cons "texmacs->latex:encoding" "utf-8")))
             (input (url-glue (url-temp) ".codex-prompt"))
             (output (url-glue (url-temp) ".codex-output"))
             (bridge (codex-bridge-path)))
        (if (== bridge "")
            (set-message "Codex bridge is not installed" "AI completion")
            (begin
              (string-save (codex-completion-prompt latex) input)
              (go-to end)
              (selection-cancel)
              (make-return-after)
              (let ((placeholder (cursor-tree)))
                (tree-set! placeholder '(athena-codex-thinking))
                (tree-go-to placeholder :end)
                (let ((buffer (current-buffer)))
                  (update-current-buffer)
                  (codex-run-completion-async
                    bridge (codex-home-path)
                    (url->system input) (url->system output)
                    (object->command
                      (lambda ()
                        (codex-finish-completion
                          buffer placeholder input output)))))))))))
