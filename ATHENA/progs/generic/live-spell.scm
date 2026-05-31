
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

(define spell-live-serial 0)
(define spell-live-engine-active? #f)

(define (spell-live-clear!)
  (when (current-buffer)
    (cancel-alt-selection "spell-live")))

(define (spell-live-stop!)
  (spell-live-clear!)
  (when spell-live-engine-active?
    (multi-spell-done)
    (set! spell-live-engine-active? #f)))

(define (spell-live-notify name val)
  (when (== val "off")
    (spell-live-stop!)))

(define-preferences
  ("live spell checking" "off" spell-live-notify)
  ("custom dictionary import language" "english" noop))

(define (spell-live-enabled?)
  (get-boolean-preference "live spell checking"))

(define (spell-live-edit-key? key)
  (and (not (string-starts? key "pre-edit:"))
       (or (== key "space")
           (== key "return")
           (== key "enter")
           (== key "backspace")
           (== key "delete")
           (== key "C-v")
           (== key "S-insert")
           (== (string-length key) 1))))

(define (spell-live-buffer-supported? buf)
  (and buf
       (not (buffer-aux? buf))
       (not (url-scratch? buf))
       (not (string-starts? (url->string buf) "tmfs://"))
       (in? (url-suffix buf) '("ath" "tm" "ts" "tp" "stm" "tmml" ""))))

(define (spell-live-active-context?)
  (and (spell-live-enabled?)
       (== (get-input-mode) 0)
       (not (in-math?))
       (spell-live-buffer-supported? (current-buffer))))

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
            (when (not spell-live-engine-active?)
              (multi-spell-start)
              (set! spell-live-engine-active? #t))
            (spell-insert lan word)
            (multi-spell-done)
            (set! spell-live-engine-active? #f)
            (spell-live-refresh! (current-buffer))
            (set-message (string-append "Added '" word "' to dictionary")
                         "spell check"))))))

(tm-menu (spell-live-popup-menu)
  ("Add misspelled word to dictionary"
   (spell-live-insert-current-word))
  ---)

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
    (when spell-live-engine-active?
      (multi-spell-done)
      (set! spell-live-engine-active? #f))
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
          (when (spell-live-active-context?)
            (spell-live-refresh! (current-buffer)))
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

(define (spell-live-filter-current-word sels)
  (let ((cur (cursor-path)))
    (let loop ((l sels) (out '()))
      (cond ((null? l) (reverse out))
            ((null? (cdr l)) (reverse out))
            ((spell-live-range-contains? (car l) (cadr l) cur)
             (loop (cddr l) out))
            (else
             (loop (cddr l) (cons (cadr l) (cons (car l) out))))))))

(define (spell-live-refresh! buf)
  (when (and (spell-live-active-context?)
             (== (url->url (current-buffer)) (url->url buf)))
    (when (not spell-live-engine-active?)
      (multi-spell-start)
      (set! spell-live-engine-active? #t))
    (let* ((t (buffer-tree))
           (p (tree->path t))
           (cp (cDr (cursor-path)))
           (pos (if (list-starts? cp p) (list-tail cp (length p)) (list)))
           (lan (get-init "language"))
           (sels (spell-live-filter-current-word
                  (tree-spell-at lan t p pos 250))))
      (if (null? sels)
          (cancel-alt-selection "spell-live")
          (set-alt-selection "spell-live" sels)))))

(define (spell-live-schedule! key)
  (when (and (spell-live-edit-key? key)
             (spell-live-active-context?))
    (set! spell-live-serial (+ spell-live-serial 1))
    (let ((serial spell-live-serial)
          (buf (current-buffer)))
      (delayed
        (:pause 450)
        (when (and (== serial spell-live-serial)
                   (spell-live-buffer-supported? buf))
          (spell-live-refresh! buf))))))

(tm-define (keyboard-press key time)
  (:require (spell-live-enabled?))
  (former key time)
  (spell-live-schedule! key))
