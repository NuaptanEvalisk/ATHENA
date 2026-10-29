
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : graphics-main.scm
;; DESCRIPTION : routines concerning the graphics as a whole (grid,
;;                                           [ change extents, etc.)
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;               (C) 2004-2007  Joris van der Hoeven and Henri Lesourd
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (graphics graphics-main)
  (:use (graphics graphics-utils)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Commutative diagrams
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-set-notebook-grid)
  (native-graphics-grid-command "notebook" "" #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3D transformations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (stree->number x)
  (string->number x))

(define (stree->vector v)
  (map stree->number (cdr v)))

(define (stree->matrix m)
  (map stree->vector (cdr m)))

(define (number->stree x)
  (number->string x))

(define (vector->stree v)
  (cons 'tuple (map number->stree v)))

(define (matrix->stree m)
  (cons 'tuple (map vector->stree m)))

(tm-define (graphics-transformation)
  (stree->matrix (tree->stree (get-env-tree "gr-transformation"))))

(tm-define (graphics-set-transformation m)
  (graphics-set-property "gr-transformation" (matrix->stree m)))

(tm-define (xz-rotation a)
  (list (list (cos a) 0.0 (sin a) 0.0)
        (list 0.0 1.0 0.0 0.0)
        (list (- (sin a)) 0.0 (cos a) 0.0)
        (list 0.0 0.0 0.0 1.0)))

(define (yz-rotation a)
  (list (list 1.0 0.0 0.0 0.0)
        (list 0.0 (cos a) (sin a) 0.0)
        (list 0.0 (- (sin a)) (cos a) 0.0)
        (list 0.0 0.0 0.0 1.0)))

(define (matrix-columns m)
  (if (null? m) 0 (length (car m))))

(define (matrix-column m i)
  (map (cut list-ref <> i) m))

(define (matrix-transpose m)
  (map (cut matrix-column m <>) (.. 0 (matrix-columns m))))

(define (vector-vector-inner v w)
  (if (or (null? v) (null? w)) 0.0
      (+ (* (car v) (car w)) (vector-vector-inner (cdr v) (cdr w)))))

(define (vector-matrix-inner v m)
  (map (cut vector-vector-inner v <>) m))

(tm-define (matrix-multiply m1 m2)
  (map (cut vector-matrix-inner <> (matrix-transpose m2)) m1))

(tm-define (graphics-rotate-xz a)
  (with m (graphics-transformation)
    (graphics-set-transformation (matrix-multiply (xz-rotation a) m))))

(tm-define (graphics-rotate-yz a)
  (with m (graphics-transformation)
    (graphics-set-transformation (matrix-multiply (yz-rotation a) m))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Grids
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Each native command plans both grids before changing the source.
(define (grid-query? property visual?)
  (== (native-graphics-grid-query property visual?) "true"))

(define (grid-number-string value)
  (cond ((string? value) value)
        ((number? value) (number->string value))
        (else "")))

(tm-define (graphics-get-grid-type visual?)
  (string->symbol (native-graphics-grid-query "type" visual?)))

(define (grids-defaulted?)
  (grid-query? "defaulted" #t))

(tm-define (graphics-reset-grids)
  (:check-mark "*" grids-defaulted?)
  (native-graphics-grid-command "reset" "" #t))

(define (visual-type-has-value? type)
  (== (cadr type) (graphics-get-grid-type #t)))

(tm-define (graphics-set-visual-grid type)
  (:check-mark "*" visual-type-has-value?)
  (native-graphics-grid-command "type" (symbol->string type) #t))

(define (edit-type-has-value? type)
  (with value (cadr type)
    (== (if (== value 'default) 'empty value)
        (graphics-get-grid-type #f))))

(tm-define (graphics-set-edit-grid type)
  (:check-mark "*" edit-type-has-value?)
  (native-graphics-grid-command "type" (symbol->string type) #f))

(tm-define (graphics-set-grid-center x y visual?)
  (native-graphics-grid-command "center" `(point ,x ,y) visual?))

(define (grid-step-has-value? val visual?)
  (string-number=? val (native-graphics-grid-query "step" visual?)))

(tm-define (graphics-set-grid-step val visual?)
  (:check-mark "*" grid-step-has-value?)
  (native-graphics-grid-command "step" val visual?))

(define (grid-astep-has-value? val visual?)
  (string-number=? val (native-graphics-grid-query "astep" visual?)))

(tm-define (graphics-set-grid-astep val visual?)
  (:check-mark "*" grid-astep-has-value?)
  (native-graphics-grid-command "astep" val visual?))

(define (grid-base-has-value? val visual?)
  (== val (native-graphics-grid-query "base" visual?)))

(tm-define (graphics-set-grid-base val visual?)
  (:check-mark "*" grid-base-has-value?)
  (native-graphics-grid-command "base" val visual?))

(tm-define (graphics-interactive-set-grid-center visual?)
  (:interactive #t)
  (interactive
    (lambda (x y) (graphics-set-grid-center x y visual?))
    "Center's x-coordinate" "Center's y-coordinate"))

(tm-define (graphics-interactive-set-grid-step visual?)
  (:interactive #t)
  (interactive (lambda (x) (graphics-set-grid-step x visual?))
    "Unit length"))

(tm-define (graphics-interactive-set-grid-astep visual?)
  (:interactive #t)
  (interactive (lambda (x) (graphics-set-grid-astep x visual?))
    "Number of angular steps"))

(tm-define (graphics-interactive-set-grid-base visual?)
  (:interactive #t)
  (interactive (lambda (x) (graphics-set-grid-base x visual?))
    "Logarithmic base"))

(tm-define (graphics-set-grid-aspect-properties c0 c1 s2 c2)
  (:argument c0 "Color axes")
  (:argument c1 "Color units")
  (:argument s2 "Subdivisions per unit")
  (:argument c2 "Color subdivisions")
  (native-graphics-grid-command "aspect-properties"
    `(tuple (tuple "axes" ,c0) (tuple "1" ,c1) (tuple ,s2 ,c2)) #t))

(define (aspect-ref a i)
  (and (pair? a) (integer? i) (> (length a) i) (list-ref a i)))

(define (nsubd-has-value? type nsubd visual?)
  (with ref (aspect-ref (native-graphics-grid-query "aspect" visual?) 3)
    (and ref
         (if nsubd
             (== (grid-number-string nsubd) (cadr ref))
             (with default-ref
                 (aspect-ref (native-graphics-grid-query "default-aspect" visual?) 3)
               (and default-ref (== (cadr ref) (cadr default-ref))))))))

(tm-define (graphics-set-grid-aspect type nsubd visual?)
  (:check-mark "*" nsubd-has-value?)
  (native-graphics-grid-command "aspect"
    `(tuple ,(symbol->string type) ,(grid-number-string nsubd)) visual?))

(tm-define (graphics-interactive-set-grid-nsubds visual?)
  (:interactive #t)
  (interactive
    (lambda (x) (graphics-set-grid-aspect 'detailed (string->number x) visual?))
    "Number of subunit steps"))

(define (grid-aspect-ofs where)
  (cond ((== where 'axes) 1)
        ((== where 'units) 2)
        ((== where 'subunits) 3)
        (else #f)))

(define (grid-color-has-value? where color)
  (let* ((i (grid-aspect-ofs (cadr where)))
         (ref (or (aspect-ref (native-graphics-grid-query "aspect" #t) i)
                  (aspect-ref (native-graphics-grid-query "aspect-props" #t) i))))
    (and ref
         (if (== color "default")
             (with default-ref
                 (aspect-ref (native-graphics-grid-query "default-aspect" #t) i)
               (and default-ref (== (caddr default-ref) (caddr ref))))
             (== color (caddr ref))))))

(tm-define (graphics-set-grid-color where color)
  (:check-mark "*" grid-color-has-value?)
  (native-graphics-grid-command "color" `(tuple ,(symbol->string where) ,color) #t))

(tm-define (grid-as-visual-grid?)
  (grid-query? "linked" #t))

(tm-define (grid-toggle-as-visual-grid)
  (:check-mark "v" grid-as-visual-grid?)
  (native-graphics-grid-command "link" "" #t))

(tm-define (grid-show-subunits?)
  (grid-query? "subunits" #t))

(tm-define (grid-toggle-show-subunits)
  (:check-mark "v" grid-show-subunits?)
  (native-graphics-grid-command "subunits" "" #t))

(define (test-visual-grid?) (grid-query? "enabled" #t))
(tm-define (graphics-toggle-visual-grid)
  (:check-mark "v" test-visual-grid?)
  (native-graphics-grid-command "toggle" "" #t))

(define (test-logical-grid?) (grid-query? "enabled" #f))
(tm-define (graphics-toggle-logical-grid)
  (:check-mark "v" test-logical-grid?)
  (native-graphics-grid-command "toggle" "" #f))

(define (test-grid?) (and (test-logical-grid?) (test-visual-grid?)))
(tm-define (graphics-toggle-grid)
  (:check-mark "v" test-grid?)
  (native-graphics-grid-command "toggle-both" "" #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Graphics edit mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-mode)
  (with m (tree->stree (get-env-tree "gr-mode"))
    (cond ((string? m)
	   `(edit ,(string->symbol m)))
          ((== m '(uninit))
           `(edit none))
          ((pair? m)
           (map string->symbol (cdr m))))))

(define (graphics-mode-has-value? mode)
  (if (and (pair? mode) (eq? (car mode) 'quote))
      (set! mode (cadr mode)))
  (== mode (graphics-mode)))

(tm-define (graphics-set-mode val)
  (:check-mark "v" graphics-mode-has-value?)
  (graphics-set-property "gr-mode" `(tuple ,@(map symbol->string val))))

(tm-define (graphics-group-mode? mode)
  (func? mode 'group-edit 1))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Provisos for graphical objects
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (graphics-get-proviso)
  (graphics-get-property "gr-proviso"))

(define (graphics-test-proviso? val)
  (with old (graphics-get-property "gr-proviso")
    (or (and (func? val 'quasiquote 1)
             (graphics-test-proviso? (cadr val)))
        (and (pair? val) (pair? old)
             (== (car val) (car old)))
        (== val old))))

(tm-define (graphics-set-proviso val)
  (:argument val "Proviso")
  (:check-mark "*" graphics-test-proviso?)
  (graphics-set-property "gr-proviso" val))

(define (update-proviso-sub l val)
  (when (and (nnull? l) (nnull? (cdr l)))
    (when (and (tm-equal? (car l) "gr-proviso")
               (tree-compound? (cadr l))
               (== (tree-arity (cadr l)) 1)
               (tree-atomic? (tree-ref (cadr l) 0)))
      (tree-set (tree-ref (cadr l) 0) val))
    (update-proviso-sub (cddr l) val)))

(tm-define (graphics-update-proviso t val)
  (when (tree-compound? t)
    (when (tree-is? t 'with)
      (update-proviso-sub (cDr (tree-children t)) val))
    (for-each (cut graphics-update-proviso <> val) (tree-children t))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Attributes for graphical objects
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (graphics-test-property? var)
  (lambda (val)
    (or (== val (graphics-get-property var))
        (and (== (graphics-get-property var) "default")
             (== val (graphics-attribute-default var))))))

(tm-define (graphics-set-opacity val)
  (:argument val "Opacity")
  (:check-mark "*" (graphics-test-property? "gr-opacity"))
  (graphics-set-property "gr-opacity" val))

(tm-define (graphics-set-color val)
  (:argument val "Color")
  (:check-mark "*" (graphics-test-property? "gr-color"))
  (graphics-set-property "gr-color" val))

(tm-define (graphics-get-pen-enhance-method)
  (with v (graphics-get-property "gr-pen-enhance")
    (cond ((== v "default") "gaussian")
          ((string? v) v)
          (else (cadr v)))))

(define (graphics-test-pen-enhance-method val)
  (when (== val "default") (set! val "gaussian"))
  (== (graphics-get-pen-enhance-method) val))

(tm-define (graphics-set-pen-enhance-method val)
  (:argument val "Pen enhance")
  (:check-mark "*" graphics-test-pen-enhance-method)
  (with strength (graphics-get-pen-enhance-strength)
    (with v (cond ((== strength "1") val)
                  ((== val "default") `(tuple "gaussian" ,strength))
                  (else `(tuple ,val ,strength)))
      (graphics-set-property "gr-pen-enhance" v))))

(tm-define (graphics-get-pen-enhance-strength)
  (with v (graphics-get-property "gr-pen-enhance")
    (if (string? v) "1" (caddr v))))

(define (graphics-test-pen-enhance-strength val)
  (== (graphics-get-pen-enhance-strength) val))

(tm-define (graphics-set-pen-enhance-strength val)
  (:argument val "Pen enhance")
  (:check-mark "*" graphics-test-pen-enhance-strength)
  (with method (graphics-get-pen-enhance-method)
    (with v (if (== val "1") method `(tuple ,method ,val))
      (graphics-set-property "gr-pen-enhance" v))))

(tm-define (graphics-set-point-style val)
  (:argument val "Point style")
  (:check-mark "*" (graphics-test-property? "gr-point-style"))
  (graphics-set-property "gr-point-style" val))

(tm-define (graphics-set-point-size val)
  (:argument val "Point size")
  (:check-mark "*" (graphics-test-property? "gr-point-size"))
  (graphics-set-property "gr-point-size" val))

(tm-define (graphics-set-point-border val)
  (:argument val "Point border")
  (:check-mark "*" (graphics-test-property? "gr-point-border"))
  (graphics-set-property "gr-point-border" val))

(tm-define (graphics-set-line-width val)
  (:argument val "Line width")
  (:check-mark "*" (graphics-test-property? "gr-line-width"))
  (graphics-set-property "gr-line-width" val))

(tm-define (graphics-set-dash-style val)
  (:argument val "Dash style")
  (:check-mark "*" (graphics-test-property? "gr-dash-style"))
  (graphics-set-property "gr-dash-style" val))

(tm-define (graphics-set-dash-style-unit val)
  (:argument val "Dash style unit")
  (:check-mark "*" (graphics-test-property? "gr-dash-style-unit"))
  (graphics-set-property "gr-dash-style-unit" val))

(tm-define (graphics-set-dash-style-unit* hu vu)
  (:argument hu "Horizontal dash style unit")
  (:argument vu "Vertical dash style unit")
  (graphics-set-property "gr-dash-style-unit" `(tuple ,hu ,vu)))

(tm-define (graphics-set-fill-color val)
  (:argument val "Fill color")
  (:check-mark "*" (graphics-test-property? "gr-fill-color"))
  (graphics-set-property "gr-fill-color" val))

(tm-define (graphics-set-arrow-begin val)
  (:argument val "Left arrow")
  (:check-mark "*" (graphics-test-property? "gr-arrow-begin"))
  (graphics-set-property "gr-arrow-begin" val))

(tm-define (graphics-set-arrow-end val)
  (:argument val "Right arrow")
  (:check-mark "*" (graphics-test-property? "gr-arrow-end"))
  (graphics-set-property "gr-arrow-end" val))

(tm-define (graphics-set-text-at-halign val)
  (:argument val "Text-at horizontal alignment")
  (:check-mark "*" (graphics-test-property? "gr-text-at-halign"))
  (graphics-set-property "gr-text-at-halign" val))

(tm-define (graphics-set-text-at-valign val)
  (:argument val "Text-at vertical alignment")
  (:check-mark "*" (graphics-test-property? "gr-text-at-valign"))
  (graphics-set-property "gr-text-at-valign" val))

(tm-define (graphics-set-text-at-repulse val)
  (:argument val "Text-at repulsive margins")
  (:check-mark "*" (graphics-test-property? "gr-text-at-repulse"))
  (graphics-set-property "gr-text-at-repulse" val))

(tm-define (graphics-set-doc-at-valign val)
  (:argument val "Document-at vertical alignment")
  (:check-mark "*" (graphics-test-property? "gr-doc-at-valign"))
  (graphics-set-property "gr-doc-at-valign" val))

(define (graphics-check-width? val)
  (if (== val "1par") (set! val "default"))
  (and (== (graphics-get-property "gr-doc-at-width") val)
       (== (graphics-get-property "gr-doc-at-hmode") "exact")))
(tm-define (graphics-set-doc-at-width val)
  (:argument val "Document-at width")
  (:check-mark "*" graphics-check-width?)
  (graphics-set-property "gr-doc-at-width" val)
  (graphics-set-property "gr-doc-at-hmode" "exact")
  (graphics-set-property "gr-doc-at-ppsep" ""))

(define (graphics-check-compact?)
  (== (graphics-get-property "gr-doc-at-hmode") "default"))
(tm-define (graphics-set-doc-at-compact)
  (:check-mark "*" graphics-check-compact?)
  (graphics-set-property "gr-doc-at-width" "default")
  (graphics-set-property "gr-doc-at-hmode" "default")
  (graphics-set-property "gr-doc-at-ppsep" "default"))

(define (graphics-toggled-property? var)
  (lambda ()
    (!= (graphics-get-property var) "default")))

(tm-define (graphics-toggle-doc-at-border)
  (:check-mark "*" (graphics-toggled-property? "gr-doc-at-border"))
  (if (== (graphics-get-property "gr-doc-at-border") "default")
      (begin
        (graphics-set-property "gr-doc-at-border" "1ln")
        (graphics-set-property "gr-doc-at-padding" "1spc"))
      (begin
        (graphics-set-property "gr-doc-at-border" "default")
        (graphics-set-property "gr-doc-at-padding" "default"))))

(tm-define (graphics-toggle-doc-at-padded)
  (:check-mark "*" (graphics-toggled-property? "gr-doc-at-padding"))
  (if (== (graphics-get-property "gr-doc-at-padding") "default")
      (graphics-set-property "gr-doc-at-padding" "1spc")
      (graphics-set-property "gr-doc-at-padding" "default")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Snapping
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (get-snap)
  (with val (graphics-get-property "gr-snap")
    (if (tm-func? val 'tuple) (tm-children val) (list "all"))))

(define (set-snap l)
  (graphics-set-property "gr-snap" `(tuple ,@l)))

(define graphics-snap-types
  (list "control point"
        "grid point" "grid curve point" "curve-grid intersection"
        "curve point" "curve-curve intersection"
         "text border point" "text border"))

(tm-define (graphics-get-snap type)
  (or (in? type (get-snap))
      (in? "all" (get-snap))))

(tm-define (graphics-test-snap? type)
  (if (== type "none")
      (null? (get-snap))
      (graphics-get-snap type)))

(tm-define (graphics-set-snap type)
  (:check-mark "*" graphics-test-snap?)
  (cond ((== type "none")
         (set-snap (list)))
        ((== type "all")
         (set-snap (list "all")))
        ((nin? type (get-snap))
         (set-snap (cons type (get-snap))))))

(tm-define (graphics-reset-snap type)
  (when (in? "all" (get-snap))
    (set-snap graphics-snap-types))
  (when (in? type (get-snap))
    (set-snap (list-remove (get-snap) type))))

(tm-define (graphics-toggle-snap type)
  (:check-mark "*" graphics-test-snap?)
  (if (graphics-get-snap type)
      (graphics-reset-snap type)
      (graphics-set-snap type)))

(tm-define (graphics-set-snap-distance val)
  (:argument val "Snap distance")
  (:check-mark "*" (graphics-test-property? "gr-snap-distance"))
  (graphics-set-property "gr-snap-distance" val))

(tm-define (graphics-set-snap-text-padding val)
  (:argument val "Text padding for snapping")
  (:check-mark "*" (graphics-test-property? "gr-text-at-snapping"))
  (graphics-set-property "gr-text-at-snapping" val))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special routines for text-at boxes and its variants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (object-get-property-bis t var)
  (tree->stree (get-env-tree var)))

(define (with-set t var val i)
  (cond ((>= i (- (tree-arity t) 1))
         (when (!= val "default")
           (tree-insert! t i (list var val))))
        ((tm-equal? (tree-ref t i) var)
         (if (!= val "default")
             (tree-assign (tree-ref t (+ i 1)) val)
             (tree-remove t i 2)))
        (else (with-set t var val (+ i 2)))))

(define (object-set-property-bis t var val)
  (cond ((tree-is? t :up 'with)
         (with-set (tree-up t) var val 0))
        ((!= val "default")
         (tree-set! t `(with ,var ,val ,t)))))

(define (object-test-property? var)
  (lambda (val)
    (if (== val "default") (set! val (tree->stree (get-init-tree var))))
    (== (object-get-property var) val)))

(tm-define (object-get-property var)
  (tree->stree (get-env-tree var)))

(tm-define (object-set-property var val)
  (and-with t (tree-innermost graphical-context?)
    (object-set-property-bis t var val)))

(tm-define (object-set-fill-color val)
  (:argument val "Fill color")
  (:check-mark "*" (object-test-property? "fill-color"))
  (object-set-property "fill-color" val))

(tm-define (object-set-text-at-halign val)
  (:argument val "Horizontal alignment")
  (:check-mark "*" (object-test-property? "text-at-halign"))
  (object-set-property "text-at-halign" val))

(tm-define (object-set-text-at-valign val)
  (:argument val "Vertical alignment")
  (:check-mark "*" (object-test-property? "text-at-valign"))
  (object-set-property "text-at-valign" val))

(tm-define (object-set-text-at-repulse val)
  (:argument val "Repulsive margin")
  (:check-mark "*" (object-test-property? "text-at-repulse"))
  (object-set-property "text-at-repulse" val))

(tm-define (object-set-doc-at-valign val)
  (:argument val "Vertical alignment")
  (:check-mark "*" (object-test-property? "doc-at-valign"))
  (object-set-property "doc-at-valign" val))

(define (object-check-width? val)
  (and (== (object-get-property "doc-at-width") val)
       (== (object-get-property "doc-at-hmode") "exact")))
(tm-define (object-set-doc-at-width val)
  (:argument val "Document-at width")
  (:check-mark "*" object-check-width?)
  (if (== val "1par") (set! val "default"))
  (object-set-property "doc-at-width" val)
  (object-set-property "doc-at-hmode" "exact")
  (object-set-property "doc-at-ppsep" ""))

(define (object-check-compact?)
  (== (object-get-property "doc-at-hmode") "min"))
(tm-define (object-set-doc-at-compact)
  (:check-mark "*" object-check-compact?)
  (object-set-property "doc-at-width" "default")
  (object-set-property "doc-at-hmode" "default")
  (object-set-property "doc-at-ppsep" "default"))

(define (object-toggled-property? var)
  (lambda ()
    (!= (object-get-property var) (get-init var))))

(tm-define (object-toggle-doc-at-border)
  (:check-mark "*" (object-toggled-property? "doc-at-border"))
  (if (== (object-get-property "doc-at-border") "0ln")
      (begin
        (object-set-property "doc-at-border" "1ln")
        (object-set-property "doc-at-padding" "1spc"))
      (begin
        (object-set-property "doc-at-border" "default")
        (object-set-property "doc-at-padding" "default"))))

(tm-define (object-toggle-doc-at-padded)
  (:check-mark "*" (object-toggled-property? "doc-at-padding"))
  (if (== (object-get-property "doc-at-padding") "0spc")
      (object-set-property "doc-at-padding" "1spc")
      (object-set-property "doc-at-padding" "default")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; UI-independent rules migrated from graphics-menu.scm
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (structured-horizontal? t)
  (:require (graphical-text-at-context? t))
  #f)
