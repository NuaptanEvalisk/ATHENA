
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : shortcut-widgets.scm
;; DESCRIPTION : native keyboard-shortcut editor bridge
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (source shortcut-widgets)
  (:use (source shortcut-edit)))

(define (shortcut-editor-entries)
  (apply append
    (map (lambda (sh) (list sh (get-user-shortcut sh)))
         (user-shortcuts-list))))

(define (shortcut-editor-apply entries)
  (let ((old (user-shortcuts-list)))
    (for (sh old) (remove-user-shortcut sh))
    (let loop ((xs entries))
      (when (and (pair? xs) (pair? (cdr xs)))
        (set-user-shortcut (car xs) (cadr xs))
        (loop (cddr xs))))))

(tm-define (open-shortcuts-editor . opt)
  (:interactive #t)
  (let* ((sh (if (null? opt) "" (car opt)))
         (cmd (if (or (null? opt) (null? (cdr opt))) "" (cadr opt)))
         (result (native-shortcut-editor sh cmd (shortcut-editor-entries))))
    (when (and (pair? result) (== (car result) "accepted"))
      (shortcut-editor-apply (cdr result)))))
