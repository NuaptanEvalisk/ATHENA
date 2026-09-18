
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : format-geometry-edit.scm
;; DESCRIPTION : routines for resizing and repositioning
;; COPYRIGHT   : (C) 2010  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic format-geometry-edit)
  (:use (utils edit selections)
        (generic embedded-edit)
        (generic format-drd)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Rigid horizontal spaces
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (space-context? t)
  (tree-is? t 'space))

(tm-define (var-space-context? t)
  (or (tree-is? t 'space)
      (tree-func? t 'separating-space 1)
      (tree-func? t 'application-space 1)))

(define (space-make-ternary t)
  (cond ((== (tm-arity t) 1) (tree-insert t 1 '("0ex" "1ex")))
	((== (tm-arity t) 2) (tree-insert t 1 '("1ex")))))

(define (space-consistent? t)
  (and (== (tm-arity t) 3)
       (lengths-consistent? (tree-ref t 1) (tree-ref t 2))))

(tm-define (geometry-speed t inc?)
  (:require (var-space-context? t))
  (with inc (if inc? 1 -1)
    (with-focus-after t
      (length-increase-step (tree-ref t 0) inc))))

(tm-define (geometry-horizontal t forward?)
  (:require (var-space-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (length-increase (tree-ref t 0) inc))))

(tm-define (geometry-vertical t down?)
  (:require (space-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (space-make-ternary t)
      (length-increase (tree-ref t 2) inc))))

(tm-define (geometry-incremental t down?)
  (:require (space-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (space-make-ternary t)
      (when (space-consistent? t)
	(length-increase (tree-ref t 1) inc)
	(length-increase (tree-ref t 2) inc)))))

(tm-define (geometry-scale t scale*)
  (:require (var-space-context? t))
  (when pinch-modified? (undo 0))
  (let* ((old (tree->stree t))
         (scale (sqrt (+ (abs scale*) 0.000001)))
         (mult (if (== (tree-arity t) 1) 1.0 0.001)))
    (for-each (cut length-scale <> scale mult) (tree-children t))
    (set! pinch-modified? (!= (tree->stree t) old))
    (tree-go-to t :end)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Rubber horizontal spaces
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (hspace-context? t)
  (tree-is? t 'hspace))

(define (rubber-space-consistent? t)
  (or (== (tm-arity t) 1)
      (and (== (tm-arity t) 3)
	   (lengths-consistent? (tree-ref t 0) (tree-ref t 1))
	   (lengths-consistent? (tree-ref t 1) (tree-ref t 2)))))

(define (rubber-space-increase t by)
  (when (rubber-space-consistent? t)
    (length-increase (tree-ref t 0) by)
    (when (== (tm-arity t) 3)
      (length-increase (tree-ref t 1) by)
      (length-increase (tree-ref t 2) by))))

(tm-define (geometry-speed t inc?)
  (:require (hspace-context? t))
  (with inc (if inc? 1 -1)
    (with-focus-after t
      (length-increase-step (tree-ref t 0) inc))))

(tm-define (geometry-horizontal t forward?)
  (:require (hspace-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (rubber-space-increase t inc))))

(tm-define (geometry-scale t scale*)
  (:require (hspace-context? t))
  (when pinch-modified? (undo 0))
  (let* ((old (tree->stree t))
         (scale (sqrt (+ (abs scale*) 0.000001)))
         (mult (if (== (tree-arity t) 1) 1.0 0.001)))
    (for-each (cut length-scale <> scale mult) (tree-children t))
    (set! pinch-modified? (!= (tree->stree t) old))
    (tree-go-to t :end)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vertical spaces
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (vspace-context? t)
  (tree-in? t '(vspace vspace*)))

(tm-define (geometry-speed t inc?)
  (:require (vspace-context? t))
  (with inc (if inc? 1 -1)
    (with-focus-after t
      (length-increase-step (tree-ref t 0) inc))))

(tm-define (geometry-vertical t down?)
  (:require (vspace-context? t))
  (with inc (if down? 1 -1)
    (with-focus-after t
      (rubber-space-increase t inc))))

(tm-define (geometry-scale t scale*)
  (:require (vspace-context? t))
  (when pinch-modified? (undo 0))
  (let* ((old (tree->stree t))
         (scale (sqrt (+ (abs scale*) 0.000001))))
    (for-each (cut length-scale <> scale 0.2) (tree-children t))
    (set! pinch-modified? (!= (tree->stree t) old))
    (tree-go-to t :end)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Vertical adjustments
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (vadjust-context? t)
  (tree-in? t (reduce-by-tag-list)))

(tm-define (geometry-speed t inc?)
  (:require (vadjust-context? t))
  (with inc (if inc? 1 -1)
    (length-increase-step (tree-ref t 1) inc)))

(tm-define (geometry-vertical t down?)
  (:require (vadjust-context? t))
  (with inc (if down? 1 -1)
    (length-increase (tree-ref t 1) inc)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Move and shift
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (move-context? t)
  (tree-in? t (move-tag-list)))

(define (set-adjust-message s c)
  (let* ((l (kbd-find-inv-system-binding '(geometry-left)))
	 (r (kbd-find-inv-system-binding '(geometry-right))))
    (if (and l r)
	(set-message (string-append s " using " l ", " r ", etc. or "
				    "via the fields in the focus bar") c)
	(set-message (string-append s " using the keyboard or "
				    "via the fields in the focus bar") c))))

(tm-define (make-move hor ver)
  (:argument hor "Horizontal")
  (:argument ver "Vertical")
  (wrap-selection-small
    (insert-go-to `(move "" ,hor ,ver) '(0 0))
    (set-adjust-message "Adjust position" "move")))

(tm-define (make-shift hor ver)
  (:argument hor "Horizontal")
  (:argument ver "Vertical")
  (wrap-selection-small
    (insert-go-to `(shift "" ,hor ,ver) '(0 0))
    (set-adjust-message "Adjust position" "shift")))

(tm-define (geometry-speed t inc?)
  (:require (move-context? t))
  (with inc (if inc? 1 -1)
    (with-focus-after t
      (length-increase-step (tree-ref t 1) inc)
      (when (not (lengths-consistent? (tree-ref t 1) (tree-ref t 2)))
        (length-increase-step (tree-ref t 2) inc)))))

(tm-define (geometry-variant t forward?)
  (:require (move-context? t))
  (circulate-unit (if forward? 1 -1)))

(tm-define (geometry-horizontal t forward?)
  (:require (move-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (replace-empty t 1 (get-zero-unit))
      (length-increase (tree-ref t 1) inc))))

(tm-define (geometry-vertical t down?)
  (:require (move-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (replace-empty t 2 (get-zero-unit))
      (length-increase (tree-ref t 2) inc))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Resize and clipped
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (resize-context? t)
  (tree-in? t (resize-tag-list)))

(tm-define (make-resize l b r t)
  (:argument l "Left")
  (:argument b "Bottom")
  (:argument r "Right")
  (:argument t "Top")
  (wrap-selection-small
    (insert-go-to `(resize "" ,l ,b ,r ,t) '(0 0))
    (set-adjust-message "Adjust extents" "resize")))

(tm-define (make-extend l b r t)
  (:argument l "Left")
  (:argument b "Bottom")
  (:argument r "Right")
  (:argument t "Top")
  (wrap-selection-small
    (insert-go-to `(extend "" ,l ,b ,r ,t) '(0 0))
    (set-adjust-message "Adjust extension" "extend")))

(tm-define (make-clipped l b r t)
  (:argument l "Left")
  (:argument b "Bottom")
  (:argument r "Right")
  (:argument t "Top")
  (wrap-selection-small
    (insert-go-to `(clipped "" ,l ,b ,r ,t) '(0 0))
    (set-adjust-message "Adjust clipping" "clipped")))

(tm-define (make-reduce-by by)
  (:argument by "Reduce by")
  (wrap-selection-small
    (insert-go-to `(reduce-by "" ,by) '(0 0))
    (set-adjust-message "Reduce vertical size" "reduce-by")))

(define (replace-empty-horizontal t)
  (replace-empty t 1 `(plus "1l" ,(get-zero-unit)))
  (replace-empty t 3 `(plus "1r" ,(get-zero-unit))))

(define (replace-empty-vertical t)
  (replace-empty t 2 `(plus "1b" ,(get-zero-unit)))
  (replace-empty t 4 `(plus "1t" ,(get-zero-unit))))

(define (resize-consistent-horizontal? t)
  (replace-empty-horizontal t)
  (lengths-consistent? (tree-ref t 1) (tree-ref t 3)))

(define (resize-consistent-vertical? t)
  (replace-empty-vertical t)
  (lengths-consistent? (tree-ref t 2) (tree-ref t 4)))

(tm-define (geometry-speed t inc?)
  (:require (resize-context? t))
  (with inc (if inc? -1 1)
    (with-focus-after t
      (length-increase-step (tree-ref t 3) inc)
      (when (not (lengths-consistent? (tree-ref t 3) (tree-ref t 4)))
        (length-increase-step (tree-ref t 3) inc)))))

(tm-define (geometry-variant t forward?)
  (:require (resize-context? t))
  (circulate-unit (if forward? 1 -1)))

(tm-define (geometry-horizontal t forward?)
  (:require (resize-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (replace-empty-horizontal t)
      (length-increase (tree-ref t 3) inc))))

(tm-define (geometry-vertical t down?)
  (:require (resize-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (replace-empty-vertical t)
      (length-increase (tree-ref t 4) inc))))

(tm-define (geometry-extremal t forward?)
  (:require (resize-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (when (resize-consistent-horizontal? t)
        (length-increase (tree-ref t 1) inc)
        (length-increase (tree-ref t 3) inc)))))

(tm-define (geometry-incremental t down?)
  (:require (resize-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (when (resize-consistent-vertical? t)
        (length-increase (tree-ref t 2) inc)
        (length-increase (tree-ref t 4) inc)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Images
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (geometry-speed t inc?)
  (:require (image-context? t))
  (with inc (if inc? 1 -1)
    (with-focus-after t
      (length-increase-step (tree-ref t 0) inc))))

(tm-define (geometry-horizontal t forward?)
  (:require (image-context? t))
  (with inc (if forward? 1 -1)
    (with-focus-after t
      (replace-empty t 1 "1w")
      (length-increase (tree-ref t 1) inc))))

(tm-define (geometry-vertical t down?)
  (:require (image-context? t))
  (with inc (if down? 1 -1)
    (with-focus-after t
      (replace-empty t 2 "1h")
      (length-increase (tree-ref t 2) inc))))

(tm-define (geometry-incremental t down?)
  (:require (image-context? t))
  (with inc (if down? -1 1)
    (with-focus-after t
      (replace-empty t 4 "0h")
      (length-increase (tree-ref t 4) inc))))

(tm-define (geometry-scale t scale*)
  (:require (image-context? t))
  (when pinch-modified? (undo 0))
  (let* ((old (tree->stree t))
         (scale (sqrt (+ (abs scale*) 0.000001)))
         (e1? (tree-empty? (tree-ref t 1)))
         (e2? (tree-empty? (tree-ref t 2)))
         (mult (if (or e1? e2?) 0.1 0.001)))
    (length-scale (tree-ref t 1) scale mult)
    (length-scale (tree-ref t 2) scale mult)
    (set! pinch-modified? (!= (tree->stree t) old))
    (tree-go-to t :end)))
