
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : graphics-group.scm
;; DESCRIPTION : editing routines for graphics group mode
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;               (C) 2004-2007  Joris van der Hoeven and Henri Lesourd
;;               (C) 2011  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (graphics graphics-group)
  (:use (graphics graphics-env)
        (graphics graphics-single)
        (kernel gui kbd-handlers)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Copy and paste attribute style
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-assign-props p obj)
  (let* ((l1 (graphics-all-attributes))
         (l2 (map gr-prefix l1))
         (l3 (map (graphics-get-property-at p) l2))
         (l4 (map cons l1 l3))
         (tab (list->ahash-table l4)))
    (graphics-remove p 'memoize-layer)
    (graphics-group-enrich-insert-table (stree-radical obj) tab #f)))

(tm-define (graphics-get-props p)
  (and-with t (path->tree p)
    (let* ((attrs (graphical-relevant-attributes t))
           (vars (list-difference attrs '("gid")))
           (get-prop (lambda (var) (graphics-path-property p var)))
           (gr-vars (map gr-prefix vars))
           (vals (map get-prop vars)))
      (for-each graphics-set-property gr-vars vals))))

(tm-define (graphics-get-props-at-mouse)
  (and-with p current-path
    (graphics-get-props p)))

(define (with-list vars vals)
  (cond ((or (null? vars) (null? vals)) (list))
        ((== (car vals) "default") (with-list (cdr vars) (cdr vals)))
        (else (cons* (car vars) (car vals)
                     (with-list (cdr vars) (cdr vals))))))

(define (graphics-tree-apply-props t vars vals)
  (with l (with-list vars vals)
    (and-with w (tree-up t)
      (if (tree-is? w 'with)
          (if (null? l)
              (tree-set! w (tm-ref w :last))
              (tree-set! w `(with ,@l ,(tm-ref w :last))))
          (if (null? l)
              (noop)
              (tree-set! t `(with ,@l ,t)))))))

(tm-define (graphics-apply-props p)
  (and-with t (path->tree p)
    (let* ((attrs (graphical-relevant-attributes t))
           (vars (list-difference attrs '("gid")))
           (gr-vars (map gr-prefix vars))
           (vals (map graphics-get-property gr-vars)))
      (graphics-tree-apply-props t vars vals))))

(tm-define (graphics-apply-props-at-mouse)
  (and-with p current-path
    (graphics-apply-props p)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Edit properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (has-attribute? t var)
  (cond ((not (tree? t)) #f)
        ((tree-is? t 'with) (has-attribute? (tm-ref t :last) var))
        ((tree-atomic? t) #f)
        (else (graphics-attribute? (tree-label t) var))))

(tm-define (graphics-mode-attribute? mode var)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (with v (if (string-starts? var "gr-") (string-drop var 3) var)
    (with l (map (cut has-attribute? <> v) (sketch-get))
      (list-or l))))

(define (property-get t var i)
  (cond ((not (tree? t)) "default")
        ((not (tree-is? t 'with)) "default")
        ((>= i (- (tree-arity t) 1)) "default")
        ((tm-equal? (tree-ref t i) var) (tree->stree (tree-ref t (+ i 1))))
        (else (property-get t var (+ i 2)))))

(define (property-and p1 p2)
  (if (== p1 p2) p1 "mixed"))

(tm-define (properties-and l)
  (cond ((null? l) "default")
        ((null? (cdr l)) (car l))
        (else (property-and (car l) (properties-and (cdr l))))))

(tm-define (graphics-get-property var)
  (:require (and (== (graphics-mode) '(group-edit edit-props))
                 (graphics-selection-active?)))
  (with v (if (string-starts? var "gr-") (string-drop var 3) var)
    (if (graphics-mode-attribute? (graphics-mode) v)
        (with l (map (cut property-get <> v 0) (sketch-get))
          (properties-and l))
        (former var))))

(define (property-remove t var i)
  (cond ((>= i (- (tree-arity t) 1)) t)
        ((tm-equal? (tree-ref t i) var)
         (if (== (tree-arity t) 3)
             (tree-remove-node! t 2)
             (tree-remove! t i 2))
         t)
        (else (property-remove t var (+ i 2)))))

(define (property-set-sub t var val i)
  (cond ((>= i (- (tree-arity t) 1))
         (tree-insert! t i (list var val))
         t)
        ((tm-equal? (tree-ref t i) var)
         (tree-set (tree-ref t (+ i 1)) val)
         t)
        (else (property-set-sub t var val (+ i 2)))))

(define (property-set t var val)
  (cond ((not (tree? t)) t)
        ((tree-is? t 'with)
         (if (== val "default")
             (property-remove t var 0)
             (property-set-sub t var val 0)))
        ((== val "default") t)
        (else
          (tree-set! t `(with ,var ,val ,t))
          t)))

(tm-define (graphics-set-property var val)
  (:require (and (== (graphics-mode) '(group-edit edit-props))
                 (graphics-selection-active?)))
  (with v (if (string-starts? var "gr-") (string-drop var 3) var)
    (if (graphics-mode-attribute? (graphics-mode) v)
        (with r (map (cut property-set <> v val) (sketch-get))
          (sketch-set! r))
        (former var val))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Legacy property-selection fallback
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Only property-edit mode still uses the Scheme sketch while its property
;; model is being rewritten.  Move/resize/rotate/group modes are native C++.
(define (edit-props-toggle-select x y p obj)
  (if (not sticky-point)
      (if multiselecting
          (let* ((ax (s2f selecting-x0))
                 (ay (s2f selecting-y0))
                 (bx (s2f x))
                 (by (s2f y))
                 (x1 (min ax bx))
                 (y1 (min ay by))
                 (x2 (max ax bx))
                 (y2 (max ay by))
                 (sel (graphics-select-area x1 y1 x2 y2)))
            (sketch-reset)
            (for (q sel)
              (sketch-toggle (path->tree q)))
            (graphics-decorations-update)
            (set! multiselecting #f)
            (set! selecting-x0 #f)
            (set! selecting-y0 #f))
          (if p
              (with t (path->tree p)
                (sketch-toggle t)
                (graphics-decorations-update))
              (begin
                (set! selecting-x0 x)
                (set! selecting-y0 y)
                (set! multiselecting #t))))))

(define (edit-props-unselect-all p)
  (cond ((nnull? (sketch-get))
         (sketch-reset)
         (graphics-decorations-update))
        ((and p (not multiselecting))
         (graphics-get-props p))))

(tm-define (toggle-select x y p obj)
  (:require (and (== (graphics-mode) '(group-edit edit-props))
                 (graphical-non-group-tag? (car obj))))
  (when (list? p)
    (and-with t (path->tree p)
      (tree-go-to t :end)))
  (edit-props-toggle-select x y p obj))

(tm-define (unselect-all p obj)
  (:require (and (== (graphics-mode) '(group-edit edit-props))
                 (graphical-non-group-tag? (car obj))))
  (edit-props-unselect-all p))

(tm-define (edit_move mode x y)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (:state graphics-state)
  (if multiselecting
      (graphical-object!
       (append
        (create-graphical-props 'default #f)
        `((with color red
            (cline (point ,selecting-x0 ,selecting-y0)
                   (point ,x ,selecting-y0)
                   (point ,x ,y)
                   (point ,selecting-x0 ,y))))))
      (graphics-decorations-update)))

(tm-define (edit_left-button mode x y)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (:state graphics-state)
  (if (and (not current-path) (graphics-selection-active?))
      (unselect-all current-path current-obj)
      (begin
        (unselect-all current-path current-obj)
        (toggle-select x y current-path current-obj))))

(tm-define (edit_right-button mode x y)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (:state graphics-state)
  (if (and (not current-path) (graphics-selection-active?))
      (unselect-all current-path current-obj)
      (toggle-select x y current-path current-obj)))

(tm-define (edit_middle-button mode x y)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (:state graphics-state)
  (if (!= (logand (get-keyboard-modifiers) ShiftMask) 0)
      (if (null? (sketch-get))
          (graphics-delete)
          (graphics-cut))
      (unselect-all current-path current-obj)))

(tm-define (edit_tab-key mode inc)
  (:require (== (graphics-mode) '(group-edit edit-props)))
  (edit_tab-key 'edit inc))

(define (group-edit-macro-arg? mode)
  (and (== (graphics-mode) '(group-edit edit-props))
       current-path
       (path->tree current-path)
       (graphical-text-arg-context? (path->tree current-path))))

(tm-define (edit_move mode x y)
  (:require (group-edit-macro-arg? mode))
  (:state graphics-state)
  (noop))

(tm-define (edit_left-button mode x y)
  (:require (group-edit-macro-arg? mode))
  (:state graphics-state)
  (noop))

(tm-define (edit_right-button mode x y)
  (:require (group-edit-macro-arg? mode))
  (:state graphics-state)
  (noop))

(tm-define (edit_middle-button mode x y)
  (:require (group-edit-macro-arg? mode))
  (:state graphics-state)
  (noop))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Cut & paste actions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-selection-active?)
  (:state graphics-state)
  (nnull? (sketch-get)))

(tm-define (graphics-copy)
  (:state graphics-state)
  (if (== (graphics-mode) '(group-edit edit-props))
      (with copied-objects (list-copy (sketch-get))
        (edit-props-unselect-all #f)
        (update-current-buffer)
        (if (null? copied-objects)
            (stree->tree "")
            (stree->tree (cons 'graphics copied-objects))))
      (stree->tree "")))

(tm-define (graphics-cut)
  (:state graphics-state)
  (if (== (graphics-mode) '(group-edit edit-props))
      (let* ((l (list-copy (sketch-get)))
             (res (graphics-copy)))
        (sketch-set! l)
        (sketch-checkout)
        (sketch-reset)
        (sketch-commit)
        res)
      (stree->tree "")))

(tm-define (graphics-paste sel)
  (:state graphics-state)
  ;;(display* "sel=" sel "\n")
  (if (and (== (graphics-mode) '(group-edit edit-props))
	   (tree-compound? sel)
	   (== (tree-label sel) 'graphics)
	   (> (tree-arity sel) 0))
      (begin
        (sketch-reset)
        (sketch-checkout)
        (foreach-number (i 0 < (tree-arity sel))
                        (sketch-toggle (tree-ref sel i)))
        (sketch-commit)
        (graphics-group-start))))
