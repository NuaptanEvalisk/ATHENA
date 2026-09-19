
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : live-spell.scm
;; DESCRIPTION : check-as-you-type spell highlighting
;; COPYRIGHT   : (C) 2026  Felix Lian
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic live-spell)
  (:use (kernel library list)))
(import-from (kernel athena tm-preferences))


;; Traversal and debounce belong to the editor's BufferActor, not a delayed
;; Scheme callback which rescans the entire document after each keystroke.

(tm-define (spell-live-replace-current-word by)
  (:interactive #t)
  (let ((sel (spell-live-current-selection))
        (buf (current-buffer)))
    (if (not sel)
        (set-message "No live spelling error at cursor" "spell check")
        (begin
          (start-editing)
          (selection-set-range-set sel)
          (clipboard-cut "dummy")
          (insert-go-to by (list (string-length by)))
          (end-editing)
          (set-message (string-append "Corrected spelling to '" by "'")
                       "spell check")))))

(tm-define (spell-live-insert-current-word)
  (:interactive #t)
  (let ((word (spell-live-current-word)))
    (if (not word)
        (set-message "No live spelling error at cursor" "spell check")
        (let ((lan (spell-live-current-language)))
          (spell-insert lan word)
          (single-spell-done lan)
          (set-message (string-append "Added '" word "' to dictionary")
                       "spell check")))))

(tm-menu (spell-live-popup-menu)
  (let* ((word (spell-live-current-word))
         (suggestions (if word (spell-live-current-suggestions) (list))))
    (assuming word
      (for (s suggestions)
        ((eval s) (spell-live-replace-current-word s)))
      (if (nnull? suggestions) ---)
      ((eval (string-append "Add '" word "' to dictionary"))
       (spell-live-insert-current-word))
      ---)))

(define (spell-live-dictionary-lines port)
  (let loop ((out '()))
    (let ((line (read-line port)))
      (if (eof-object? line) (reverse out)
          (let ((word (tm-string-trim-both line)))
            (loop (if (== word "") out (cons word out))))))))

(tm-define (spell-live-import-custom-dictionary lan name)
  (:interactive #t)
  (let* ((file (url->system name))
         (port (open-input-file file))
         (words (list-remove-duplicates (spell-live-dictionary-lines port)))
         (count 0)
         (result #f))
    (close-input-port port)
    (set! result (single-spell-start lan))
    (if (!= result "ok")
        (set-message result "import dictionary")
        (begin
          (for-each
            (lambda (word)
              (spell-insert lan word)
              (set! count (+ count 1)))
            words)
          (single-spell-done lan)
          (let ((msg (string-append "Imported " (number->string count)
                                    " words into " lan " dictionary")))
            (set-message msg "import dictionary")
            (notify-now msg))))))

(tm-define (spell-live-import-custom-dictionary-from-preferences)
  (:interactive #t)
  (let* ((pref (get-preference "custom dictionary import language"))
         (lan (if (== pref "") "english" pref)))
    (choose-file
     (lambda (name) (spell-live-import-custom-dictionary lan name))
     "Import custom dictionary" "")))
