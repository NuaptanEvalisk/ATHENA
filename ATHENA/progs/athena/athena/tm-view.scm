
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-view.scm
;; DESCRIPTION : setting the view preferences and properties
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-view)
  (:use (utils library cursor)))
(import-from (kernel athena tm-preferences))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; View preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define saved-informative-flags "default")

(tm-define (toggle-full-screen-mode)
  (:synopsis "Toggle full screen mode")
  (:check-mark "v" full-screen?)
  (if (full-screen?)
      (begin
        (init-env "info-flag" saved-informative-flags)
        (full-screen-mode #f #f)
	(restore-zoom (get-init-page-rendering)))
      (begin
	(save-zoom (get-init-page-rendering))
        (set! saved-informative-flags (get-init "info-flag"))
        (init-env "info-flag" "none")
        (full-screen-mode #t #f)
        (fit-to-screen))))

(tm-define (toggle-full-screen-edit-mode)
  (:synopsis "Toggle full screen edit mode")
  (:check-mark "v" full-screen-edit?)
  (let* ((old (full-screen?))
	 (new (not (full-screen-edit?))))
    (when (and (not old) new)
      (save-zoom (get-init-page-rendering)))
    (full-screen-mode new new)
    (when (and old (not new))
      (restore-zoom (get-init-page-rendering)))))

(define panorama-revert (make-ahash-table))
(define (panorama-mode?) (== (get-init-page-rendering) "panorama"))
(tm-define (toggle-panorama-mode)
  (:synopsis "Toggle panorama screen rendering")
  (:check-mark "v" panorama-mode?)
  (cond ((slideshow-mode?)
         (toggle-slideshow-mode)
         (delayed (:idle 25) (toggle-panorama-mode)))
        ((panorama-mode?)
         (with old (or (ahash-ref panorama-revert (current-buffer)) "paper")
           (ahash-remove! panorama-revert (current-buffer))
           (init-page-rendering old)))
        (else
         (with old (get-init-page-rendering)
           (ahash-set! panorama-revert (current-buffer) old)
           (init-page-rendering "panorama")))))

(define slideshow-revert (make-ahash-table))
(define (slideshow-mode?) (== (get-init-page-rendering) "slideshow"))
(tm-define (toggle-slideshow-mode)
  (:synopsis "Toggle slideshow screen rendering")
  (:check-mark "v" slideshow-mode?)
  (cond ((panorama-mode?)
         (toggle-panorama-mode)
         (delayed (:idle 25) (toggle-slideshow-mode)))
        ((slideshow-mode?)
         (with old (or (ahash-ref slideshow-revert (current-buffer)) "paper")
           (ahash-remove! slideshow-revert (current-buffer))
           (init-page-rendering old)))
        (else
         (with old (get-init-page-rendering)
           (ahash-set! slideshow-revert (current-buffer) old)
           (init-page-rendering "slideshow")))))

(define (persistent-fit-width?)
  (get-boolean-preference "persistent fit width"))

(define (persistent-fit-width-applicable?)
  (in? (get-init-page-rendering) (list "paper" "papyrus")))

(define persistent-fit-width-count 0)
(define fit-width-editing-position-count 0)

(define (restore-editing-position sx sy cx cy)
  (let ((dx (- cx sx))
        (dy (- cy sy)))
    (set-scroll (- (get-cursor-x) dx) (- (get-cursor-y) dy))))

(define (fit-to-screen-width-preserve-editing-position)
  (with (sx sy cx cy) (list (get-scroll-x) (get-scroll-y)
                            (get-cursor-x) (get-cursor-y))
    (let ((cp (cursor-path)))
      (set! fit-width-editing-position-count
            (+ fit-width-editing-position-count 1))
      (with current fit-width-editing-position-count
        (fit-to-screen-width)
        (for-each
         (lambda (delay)
           (delayed (:idle delay)
             (when (and (== current fit-width-editing-position-count)
                        (== cp (cursor-path)))
               (restore-editing-position sx sy cx cy))))
         '(1 25))))))

(define (fit-persistent-to-screen-width)
  (when (persistent-fit-width-applicable?)
    (fit-to-screen-width-preserve-editing-position)))

(tm-define (schedule-persistent-fit-width)
  (:synopsis "Schedule persistent fit to width")
  (when (and (persistent-fit-width?)
             (persistent-fit-width-applicable?))
    (set! persistent-fit-width-count (+ persistent-fit-width-count 1))
    (with current persistent-fit-width-count
      ;; Qt ADS pane widths and page metrics may settle after the first idle
      ;; pass; repeat the fit briefly, while cancelling older resize bursts.
      (for-each
       (lambda (delay)
         (delayed (:idle delay)
           (when (and (persistent-fit-width?)
                      (persistent-fit-width-applicable?)
                      (== current persistent-fit-width-count))
             (fit-persistent-to-screen-width))))
       '(25 100 250 600)))))

(tm-define (toggle-persistent-fit-width)
  (:synopsis "Toggle persistent fit to width")
  (:check-mark "v" (lambda ()
                     (and (persistent-fit-width?)
                          (persistent-fit-width-applicable?))))
  (toggle-preference "persistent fit width")
  (when (and (persistent-fit-width?)
             (persistent-fit-width-applicable?))
    (fit-persistent-to-screen-width)
    (schedule-persistent-fit-width)))

(tm-define (window-resize-notifier name)
  (schedule-persistent-fit-width))
