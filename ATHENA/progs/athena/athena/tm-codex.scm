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

(define (codex-completion-prompt latex figures?)
  (string-append
    "Continue the following ATHENA document selection. It was exported as "
    "a LaTeX snippet. Return only the LaTeX snippet that should be inserted "
    "immediately after it: no Markdown fence, explanation, or repetition of "
    "the supplied text. Preserve its language, mathematical notation, style, "
    "and level of formality."
    (if figures?
        (string-append
          " Figure placeholders of the form <codex-fig-UUID-N.png> refer "
          "to the attached images with matching labels. Use their visual "
          "content when continuing the document.")
        "")
    "\n\n"
    latex))

(define (codex-bridge-path)
  (if (url-exists? "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize "$ATHENA_PATH/bin/athena-codex-bridge")
      (url-concretize
        (url-resolve-in-path "athena-codex-bridge"))))

(define (codex-initialize-model-catalog)
  (let ((bridge (codex-bridge-path)))
    (when (!= bridge "")
      (codex-initialize-models bridge (codex-home-path)))))

(delayed (:idle 100) (codex-initialize-model-catalog))

(define (codex-remove-completion-files input output figures)
  (when (url-exists? input) (url-remove input))
  (when (url-exists? output) (url-remove output))
  (for-each
    (lambda (figure)
      (when (url-exists? figure) (url-remove figure)))
    figures))

(define codex-visual-node-labels
  '(image graphics commutative-diagram))

(define (codex-render-selection-figure file figure)
  (let ((rendered? #f))
    (catch #t
      (lambda ()
        (print-snippet file figure #t)
        (set! rendered? (url-exists? file)))
      (lambda (key . args)
        (display* "Codex figure rendering failed: " key " " args "\n")))
    (when (and (not rendered?) (url-exists? file))
      (url-remove file))
    rendered?))

(define (codex-prepare-selection-figures selection)
  (let ((uuid (vault-generate-uuid))
        (figure-number 0)
        (figures '())
        (markers '()))
    (define (rebuild t)
      (stree->tree
        (cons (tree-label t)
              (map (lambda (child) (tree->stree (visit child)))
                   (tree-children t)))))
    (define (visit t)
      (cond
        ((tree-atomic? t) (tree-copy t))
        ((in? (tree-label t) codex-visual-node-labels)
         (let* ((number (+ figure-number 1))
                (filename
                  (string-append "codex-fig-" uuid "-"
                                 (number->string number) ".png"))
                (marker
                  (string-append "ATHENACODEXFIG"
                    (string-replace uuid "-" "") "N"
                    (number->string number) "PNG"))
                (placeholder (string-append "<" filename ">"))
                (file (url-append (url-temp-dir) filename)))
           (if (codex-render-selection-figure file t)
               (begin
                 (set! figure-number number)
                 (set! figures (cons file figures))
                 (set! markers (cons (cons marker placeholder) markers))
                 (stree->tree marker))
               (rebuild t))))
        (else (rebuild t))))
    (list (visit selection) (reverse figures) (reverse markers))))

(define (codex-substitute-figure-markers latex markers)
  (if (null? markers) latex
      (codex-substitute-figure-markers
        (string-replace latex (caar markers) (cdar markers))
        (cdr markers))))

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

(define (codex-finish-completion buffer placeholder input output figures)
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
  (codex-remove-completion-files input output figures))

(define (codex-ai-completion-with-options model effort service-tier web-search)
  (if (not (selection-active-any?))
      (set-message "Select text to continue with Codex" "AI completion")
      (let ((bridge (codex-bridge-path)))
        (if (== bridge "")
            (set-message "Codex bridge is not installed" "AI completion")
            (let* ((selection (selection-tree))
                   (end (selection-get-end))
                   (prepared (codex-prepare-selection-figures selection))
                   (selection* (list-ref prepared 0))
                   (figures (list-ref prepared 1))
                   (markers (list-ref prepared 2))
                   (latex* (convert (tm->stree selection*)
                                    "texmacs-stree" "latex-snippet"
                                    (cons "texmacs->latex:encoding" "utf-8")))
                   (latex (codex-substitute-figure-markers latex* markers))
                   (input (url-glue (url-temp) ".codex-prompt"))
                   (output (url-glue (url-temp) ".codex-output")))
              (string-save
                (codex-completion-prompt latex (not (null? figures))) input)
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
                    model effort service-tier web-search
                    (map url->system figures)
                    (object->command
                      (lambda ()
                        (codex-finish-completion
                          buffer placeholder input output figures)))))))))))

(tm-define (codex-ai-completion)
  (codex-ai-completion-with-options "" "" "" ""))

(tm-define (codex-ai-completion-custom)
  (if (not (selection-active-any?))
      (set-message "Select text to continue with Codex" "AI completion")
      (let ((options
              (codex-completion-options
                (codex-bridge-path) (codex-home-path))))
        (when (and (list? options) (= (length options) 4))
          (codex-ai-completion-with-options
            (list-ref options 0)
            (list-ref options 1)
            (list-ref options 2)
            (if (== (list-ref options 3) "on")
                "live" "disabled"))))))
