
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-tools.scm
;; DESCRIPTION : various tools
;; COPYRIGHT   : (C) 2012  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-tools))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document statistics
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (count-characters doc)
  (with s (convert doc "texmacs-tree" "verbatim-snippet")
    (if (string? s) (string-length s) 0)))

(define (compress-spaces s)
  (let* ((s1 (string-replace s "\n" " "))
         (s2 (string-replace s1 "\t" " "))
         (s3 (string-replace s2 "  " " "))
         (s4 (if (string-starts? s3 " ") (string-drop s3 1) s3))
         (s5 (if (string-ends? s4 " ") (string-drop-right s4 1) s4)))
    (if (== s5 s) s (compress-spaces s5))))

(tm-define (count-words doc)
  (with s (convert doc "texmacs-tree" "verbatim-snippet")
    (if (string? s)
        (length (string-tokenize-by-char (compress-spaces s) #\space))
        0)))

(tm-define (count-lines doc)
  (with s (convert doc "texmacs-tree" "verbatim-snippet")
    (if (string? s)
        (length (string-tokenize-by-char s #\newline))
        0)))

(tm-define (selection-or-document)
  (if (selection-active-any?)
      (selection-tree)
      (buffer-tree)))

(tm-define (show-character-count)
  (with nr (count-characters (selection-or-document))
    (show-message (string-append "Character count: " (number->string nr)) "Statistics")))

(tm-define (show-word-count)
  (with nr (count-words (selection-or-document))
    (show-message (string-append "Word count: " (number->string nr)) "Statistics")))

(tm-define (show-line-count)
  (with nr (count-lines (selection-or-document))
    (show-message (string-append "Line count: " (number->string nr)) "Statistics")))

(tm-define (center-footer-hook s)
  (let* ((s* (if (tree? s) (tree->string s) s))
         (stats? (get-boolean-preference "gui:live-statistics")))
    (if (and stats? (string-null? s*))
        (with t (buffer-tree)
          (with nr-c (count-characters t)
            (with nr-w (count-words t)
              (with nr-l (count-lines t)
                (string-append "Words: " (number->string nr-w)
                               ", Chars: " (number->string nr-c)
                               ", Lines: " (number->string nr-l))))))
        s*)))

(define (save-aux-enabled?) (== (get-env "save-aux") "true"))
(tm-define (toggle-save-aux)
  (:synopsis "Toggle whether we save auxiliary data")
  (:check-mark "v" save-aux-enabled?)
  (let ((new (if (== (get-env "save-aux") "true") "false" "true")))
    (init-env "save-aux" new)))

(tm-define (toggle-show-kbd)
  (:synopsis "Toggle whether we show keyboard presses")
  (:check-mark "v" get-show-kbd)
  (set-show-kbd (not (get-show-kbd))))

(tm-define (clear-font-cache)
  (:synopsis "Clear font cache under ATHENA_HOME_PATH")
  (map system-remove
    (list
      "$ATHENA_HOME_PATH/system/cache/font_cache.scm"
      "$ATHENA_HOME_PATH/fonts/font-database.scm"
      "$ATHENA_HOME_PATH/fonts/font-features.scm"
      "$ATHENA_HOME_PATH/fonts/font-characteristics.scm")))

(tm-define (scan-disk-for-fonts)
  (:interactive #t)
  (:synopsis "Scan disk for more fonts")
  (system-wait "Full search for more fonts on your system"
               "(can be long)")
  (font-database-build-local))

(tm-define (clean-athena-cache)
  (:interactive #t)
  (:synopsis "Clean the system cache")
  (user-confirm "Are you sure you want to clean the system cache?" #f
    (lambda (answ)
      (when answ
        (system "rm -rf ~/.ATHENA/system/cache")
        (restart-message)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Miscellaneous
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (picture-gc)
  (picture-cache-reset)
  (update-all-buffers))
