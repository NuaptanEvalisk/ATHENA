
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

(define (spell-live-range-contains? start end p)
  (and (path-less-eq? start p)
       (path-less? p end)))

(define (spell-live-selection->string sel)
  (and (list-2? sel)
       (== (cDr (car sel)) (cDr (cadr sel)))
       (with t (path->tree (cDr (car sel)))
         (and (tree-atomic? t)
              (let* ((s (tree->string t))
                     (n (string-length s))
                     (i1 (cAr (car sel)))
                     (i2 (cAr (cadr sel))))
                (and (>= i1 0) (> i2 i1) (>= n i2)
                     (substring s i1 i2)))))))

(define (spell-live-current-selection)
  (let ((cur (cursor-path)))
    (let loop ((sels (get-alt-selection "spell-live")))
      (cond ((null? sels) #f)
            ((null? (cdr sels)) #f)
            ((spell-live-range-contains? (car sels) (cadr sels) cur)
             (list (car sels) (cadr sels)))
            (else (loop (cddr sels)))))))

(tm-define (spell-live-current-word)
  (and-with sel (spell-live-current-selection)
    (spell-live-selection->string sel)))

(define (spell-live-suggestions sel)
  (and-with word (spell-live-selection->string sel)
    (let* ((lan (spell-live-selection-language sel))
           (st (tm->stree (spell-check lan word)))
           (l0 (if (tm-func? st 'tuple) (cdr st) (list)))
           (l1 (if (null? l0) l0 (cdr l0))))
      (if (<= (length l1) 9) l1 (sublist l1 0 9)))))

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

(define (spell-live-selection-language sel)
  (let* ((bt (buffer-tree))
         (rp (tree->path bt))
         (sp (car sel))
         (p (and (list-starts? sp rp) (sublist sp (length rp) (length sp))))
         (lan (get-init "language")))
    (if (not p) lan
        (tm->stree (tree-descendant-env bt (cDr p) "language" lan)))))

(tm-define (spell-live-insert-current-word)
  (:interactive #t)
  (let ((sel (spell-live-current-selection)))
    (if (not sel)
        (set-message "No live spelling error at cursor" "spell check")
        (and-with word (spell-live-selection->string sel)
          (let ((lan (spell-live-selection-language sel)))
            (spell-insert lan word)
            (single-spell-done lan)
            (set-message (string-append "Added '" word "' to dictionary")
                         "spell check"))))))

(tm-menu (spell-live-popup-menu)
  (let* ((sel (spell-live-current-selection))
         (word (and sel (spell-live-selection->string sel)))
         (suggestions (or (and sel (spell-live-suggestions sel)) (list))))
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
