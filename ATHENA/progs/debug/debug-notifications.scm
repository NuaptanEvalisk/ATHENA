
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : debug-notifications.scm
;; DESCRIPTION : notification bridge for the native Error messages pane
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (debug debug-notifications))
(import-from (kernel athena tm-preferences))

(define error-message-updating? #f)
(define error-message-errors? #f)
(define error-message-warnings? #f)
(define error-message-generation 0)
(define error-message-acknowledged-generation 0)

(tm-define (acknowledge-debug-messages)
  (set! error-message-acknowledged-generation error-message-generation))

(define (show-new-error-messages generation)
  (when (> generation error-message-acknowledged-generation)
    (acknowledge-debug-messages)
    (error-messages-show)))

(define (update-error-message-pane)
  (let ((generation error-message-generation))
    (when (or (and error-message-errors?
                   (get-boolean-preference "open console on errors"))
              (and error-message-warnings?
                   (get-boolean-preference "open console on warnings")))
      (delayed (:idle 1) (show-new-error-messages generation)))
    (set! error-message-updating? #f)
    (set! error-message-errors? #f)
    (set! error-message-warnings? #f)))

(tm-define (notify-debug-message channel)
  (set! error-message-generation (+ error-message-generation 1))
  (when (string-ends? channel "-error") (set! error-message-errors? #t))
  (when (string-ends? channel "-warning") (set! error-message-warnings? #t))
  (when (not error-message-updating?)
    (set! error-message-updating? #t)
    (update-error-message-pane)))
