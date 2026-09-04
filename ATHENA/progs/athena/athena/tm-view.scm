
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

(define (notify-header var val)
  (show-header (== val "on")))

(define (notify-icon-bar var val)
  (cond ((== var "main icon bar")
         (show-icon-bar 0 (== val "on")))
        ((== var "mode dependent icons")
         (show-icon-bar 1 (== val "on")))
        ((== var "focus dependent icons")
         (show-icon-bar 2 (== val "on")))
        ((== var "user provided icons")
         (show-icon-bar 3 (== val "on")))))

(define (notify-toolbar-presentation var val)
  (when (current-view)
    (delayed (:idle 0) (notify-change 256))))

(define (notify-status-bar var val)
  (show-footer (== val "on")))

(define (notify-bottom-tools var val)
  (cond ((== var "bottom tools")
         (show-bottom-tools 0 (== val "on")))
        ((== var "extra tools")
         (show-bottom-tools 1 (== val "on")))))

(define (notify-zoom-factor var val)
  (with z (string->number val)
    (set! z (max (min z 25.0) 0.04))
    (set-default-zoom-factor z)
    (set-window-zoom-factor z)))

(define (notify-remote-control var val)
  (ahash-set! remote-control-remap val var))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Changing the view properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (toggle-visible-header)
  (:synopsis "Toggle the visibility of the window's header")
  (:check-mark "v" visible-header?)
  (with val (not (visible-header?))
    (if (and (== (windows-number) 1) (os-macos?))
        (set-boolean-preference "header" val)
        (show-header val))))

(tm-define (toggle-visible-footer)
  (:synopsis "Toggle the visibility of the window's footer")
  (:check-mark "v" visible-footer?)
  (with val (not (visible-footer?))
    (if (== (windows-number) 1)
        (set-boolean-preference "status bar" val)
        (show-footer val))))

(tm-define (toggle-visible-bottom-tools n)
  (:synopsis "Toggle the visibility of the bottom tools")
  (:check-mark "v" visible-bottom-tools?)
  (with val (not (visible-bottom-tools? n))
    (with var (if (== n 0) "bottom tools" "extra tools")
      (if (and (== (windows-number) 1) (in? n (list 0 1)))
          (set-boolean-preference var val)
          (show-bottom-tools n val)))))

(tm-define (toggle-visible-icon-bar n)
  (:synopsis "Toggle the visibility of the @n-th icon bar")
  (:check-mark "v" visible-icon-bar?)
  (let* ((val (not (visible-icon-bar? n)))
         (var (cond ((== n 0) "main icon bar")
                    ((== n 1) "mode dependent icons")
                    ((== n 2) "focus dependent icons")
                    ((== n 3) "user provided icons"))))
    (if (== (windows-number) 1)
        (set-boolean-preference var val)
        (show-icon-bar n val))
    (when (and (os-macos?) (== n 0)
               (get-boolean-preference "use unified toolbar"))
      (notify-now "Restart ATHENA to avoid potential visual artefacts"))))

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

(tm-define (toggle-remote-control-mode)
  (:synopsis "Toggle remote keyboard control mode")
  (:check-mark "v" remote-control-mode?)
  (set! remote-control-flag? (not remote-control-flag?)))

(define (test-zoom-factor? z)
  (<= (abs (- (get-window-zoom-factor) (eval z))) 0.01))

(tm-define (change-zoom-factor z)
  (:check-mark "*" test-zoom-factor?)
  (set! z (max (min z 25.0) 0.04))
  (when (and (== (windows-number) 1)
             (in? (get-init "page-packet") (list "1" "2")))
    (set-preference "zoom factor" (number->string z)))
  (set-window-zoom-factor z)
  (notify-page-change)
  (notify-change 1))

(tm-define (other-zoom-factor s)
  (:argument s "Zoom factor")
  (if (string-ends? s "%")
      (with p (string->number (string-drop-right s 1))
        (change-zoom-factor (* 0.01 p)))
      (change-zoom-factor (string->number s))))

(define zoom-table (make-ahash-table))

(tm-define (save-zoom mode)
  (with key (list (current-buffer) mode (full-screen?))
    (ahash-set! zoom-table key (get-window-zoom-factor))))

(tm-define (restore-zoom mode)
  (with key (list (current-buffer) mode (full-screen?))
    (and-with zf (ahash-ref zoom-table key)
      (when (!= zf (get-window-zoom-factor))
        (change-zoom-factor zf)))))

(define (normalize-zoom-sub zoom l)
  (cond ((null? l) zoom)
        ((< (abs (- zoom (car l))) (* 0.02 zoom)) (car l))
        (else (normalize-zoom-sub zoom (cdr l)))))

(define (normalize-zoom zoom)
  (with std-zooms (map (lambda (x) (exp (* x (/ (log 2.0) 4.0))))
                       (.. -10 10))
    (normalize-zoom-sub zoom std-zooms)))

(tm-define (zoom-in x)
  (let* ((old (get-window-zoom-factor))
         (new (normalize-zoom (* x old))))
    (change-zoom-factor new)))

(tm-define (zoom-out x)
  (zoom-in (/ 1.0 x)))

(define (fit-screen-zoom-scale)
  (max 0.0001 (* (get-retina-zoom) (get-retina-scale))))

(define (change-fit-zoom-factor f)
  ;; Fit formulas operate in editor/canvas zoom space.  ATHENA stores the
  ;; user-visible window zoom without the HiDPI window scale, so convert back
  ;; before committing the fitted value.
  (let* ((target (/ (- f 0.0001) (fit-screen-zoom-scale)))
         (current (get-window-zoom-factor))
         (tolerance (* 0.001 (max 1.0 (abs target)))))
    (when (> (abs (- target current)) tolerance)
      (change-zoom-factor target))))

(define (fit-canvas-zoom-factor)
  (* (get-window-zoom-factor) (fit-screen-zoom-scale)))

(tm-define (fit-all-to-screen)
  (let* ((zf (fit-canvas-zoom-factor))
         (ww (get-window-width))
         (tw (get-total-width #f))
         (dw (- (get-total-width #t) tw))
         (wf (/ (- ww (* zf dw)) tw))
         (wh (get-window-height))
         (th (get-total-height #f))
         (dh (- (get-total-height #t) th))
         (hf (/ (- wh (* zf dh)) th))
         (f (min wf hf)))
    (change-fit-zoom-factor f)))

(tm-define (fit-to-screen)
  (let* ((zf (fit-canvas-zoom-factor))
         (ww (get-window-width))
         (pw (get-pages-width #f))
         (dw (- (get-pages-width #t) pw))
         (wf (/ (- ww (* zf dw)) pw))
         (wh (get-window-height))
         (ph (get-page-height #f))
         (dh (- (get-page-height #t) ph))
         (hf (/ (- wh (* zf dh)) ph))
         (f (min wf hf)))
    (change-fit-zoom-factor f)))

(tm-define (fit-to-screen-width)
  (let* ((zf (fit-canvas-zoom-factor))
         (ww (get-window-width))
         (pw (get-pages-width #f))
         (dw (- (get-pages-width #t) pw))
         (f (/ (- ww (* zf dw)) pw)))
    (change-fit-zoom-factor f)))

(tm-define (fit-to-screen-height)
  (let* ((zf (fit-canvas-zoom-factor))
         (wh (get-window-height))
         (ph (get-page-height #f))
         (dh (- (get-page-height #t) ph))
         (f (/ (- wh (* zf dh)) ph)))
    (change-fit-zoom-factor f)))

(define (snap-to-pages?)
  (get-boolean-preference "snap to pages"))

(tm-define (toggle-snap-to-pages)
  (:synopsis "Toggle page snapping")
  (:check-mark "v" snap-to-pages?)
  (toggle-preference "snap to pages"))

(define (persistent-fit-width?)
  (get-boolean-preference "persistent fit width"))

(define (persistent-fit-width-applicable?)
  (!= (get-init-page-rendering) "automatic"))

(define (typewriter-mode?)
  (get-boolean-preference "typewriter mode"))

(define persistent-fit-width-count 0)
(define resize-editing-position-count 0)
(define fit-width-editing-position-count 0)

(define (restore-editing-position sx sy cx cy)
  (let ((dx (- cx sx))
        (dy (- cy sy)))
    (set-scroll (- (get-cursor-x) dx) (- (get-cursor-y) dy))))

(define (schedule-editing-position-restore delays)
  (let ((sx (get-scroll-x))
        (sy (get-scroll-y))
        (cx (get-cursor-x))
        (cy (get-cursor-y))
        (cp (cursor-path)))
    (set! resize-editing-position-count (+ resize-editing-position-count 1))
    (with current resize-editing-position-count
      (for-each
       (lambda (delay)
         (delayed (:idle delay)
           (when (and (== current resize-editing-position-count)
                      (== cp (cursor-path)))
             (restore-editing-position sx sy cx cy))))
       delays))))

(tm-define (schedule-resize-editing-position)
  (:synopsis "Restore editing position after reflow resize")
  (schedule-editing-position-restore '(25 100 250 600)))

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

(tm-define (toggle-typewriter-mode)
  (:synopsis "Keep the editing line near the vertical center in scroll view")
  (:check-mark "v" typewriter-mode?)
  (toggle-preference "typewriter mode"))

(tm-define (window-resize-notifier name)
  (schedule-persistent-fit-width))

(register-preference-callback-procedures
  (list notify-header notify-icon-bar notify-toolbar-presentation
        notify-remote-control notify-status-bar notify-zoom-factor))
