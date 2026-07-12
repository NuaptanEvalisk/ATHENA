
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : graphics-markup.scm
;; DESCRIPTION : extra graphical macros
;; COPYRIGHT   : (C) 2012  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (graphics graphics-markup)
  (:use (graphics graphics-drd)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Definition of graphical macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (ca*r x) (if (pair? x) (ca*r (car x)) x))

(tm-define-macro (define-graphics head . l)
  (receive (opts body) (list-break l not-define-option?)
    `(begin
       (set! gr-tags-user (cons ',(ca*r head) gr-tags-user))
       (tm-define ,head ,@opts (:secure #t) ,@body))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Useful subroutines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (tm-point? p) (tm-func? p 'point 2))
(tm-define (tm-x p) (tm-ref p 0))
(tm-define (tm-y p) (tm-ref p 1))

(tm-define (tm->number t)
  (if (tm-atomic? t) (string->number (tm->string t)) 0))

(tm-define (number->tm x)
  (number->string x))

(tm-define (point->complex p)
  (make-rectangular (tm->number (tm-x p)) (tm->number (tm-y p))))

(tm-define (complex->point z)
  `(point ,(number->tm (real-part z)) ,(number->tm (imag-part z))))

(tm-define (graphics-transform fun g)
  (cond ((tm-point? g) (fun g))
        ((tm-atomic? g) g)
        (else (cons (tm-car g)
                    (map (cut graphics-transform fun <>)
                         (tm-children g))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic macros
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-graphics (rectangle P1 P2)
  (let* ((p1 (if (tm-point? P1) P1 '(point "0" "0")))
         (p2 (if (tm-point? P2) P2 p1)))
    `(cline ,p1 (point ,(tm-x p2) ,(tm-y p1))
            ,p2 (point ,(tm-x p1) ,(tm-y p2)))))

(define-graphics (circle C P)
  (let* ((c  (if (tm-point? C) C '(point "0" "0")))
         (p  (if (tm-point? P) P c))
         (cx (tm-x c)) (cy (tm-y c))
         (px (tm-x p)) (py (tm-y p))
         (dx `(minus ,px ,cx)) (dy `(minus ,py ,cy))
         (q1 `(point (minus ,cx ,dx) (minus ,cy ,dy)))
         (q2 `(point (minus ,cx ,dy) (plus ,cy ,dx))))
    `(superpose (with "point-style" "none" ,c) (carc ,p ,q1 ,q2))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Electrical diagrams
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define ((rescale z0 dz) p)
  (complex->point (+ z0 (* dz (point->complex p)))))

(tm-define (electrical im scale p1 p2 p3)
  (let* ((z1 (if (tm-point? p1) (point->complex p1) 0))
         (z2 (if (tm-point? p2) (point->complex p2) z1))
         (z3 (if (tm-point? p3) (point->complex p3) z2))
         (dz (- z2 z1))
         (l  (magnitude dz))
         (d1 (if (= dz 0) 0 (abs (* l (imag-part (/ (- z3 z1) dz))))))
         (d2 (/ (min l (/ d1 scale)) 2))
         (u  (if (= dz 0) 0 (* d2 (/ dz l))))
         (vm (/ (+ z1 z2) 2))
         (v1 (- vm u))
         (v2 (+ vm u))
         (rescaler (rescale v1 (- v2 v1))))
    `(superpose
      (line ,p1 ,(complex->point v1))
      ,(graphics-transform rescaler im)
      (line ,(complex->point v2) ,p2)
      (with "point-style" "none" ,p3))))

(define (std-condensator)
  `(superpose
     (line (point "0" "-2") (point "0" "2"))
     (line (point "1" "-2") (point "1" "2"))))

(define-graphics (condensator p1 p2 p3)
  (electrical (std-condensator) 2 p1 p2 p3))

(define (std-diode)
  `(superpose
     (cline (point "0" "-0.5") (point "1" "0") (point "0" "0.5"))
     (line (point "1" "-0.5") (point "1" "0.5"))))

(define-graphics (diode p1 p2 p3)
  (electrical (std-diode) 0.5 p1 p2 p3))

(define (std-battery)
  `(superpose 
    (line (point "0" "-2") (point "0" "2"))
    (line (point "0.333" "-1") (point "0.333" "1"))
    (line (point "0.666" "-2") (point "0.666" "2"))
    (line (point "1" "-1") (point "1" "1"))))

(define-graphics (battery p1 p2 p3)
  (electrical (std-battery) 1.5 p1 p2 p3))

(define (std-resistor)
  `(line (point "0" "0.0") (point "0.10" "0.0") 
         (point "0.17" "0.13") (point "0.302" "-0.13")
         (point "0.434" "0.13") (point "0.566" "-0.13")
         (point "0.698" "0.13") (point "0.83" "-0.13") 
         (point "0.90" "0.0") (point "1" "0.0")))

(define-graphics (resistor p1 p2 p3)
  (electrical (std-resistor) 0.2 p1 p2 p3))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Triangle with text inside
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-group graphical-contains-curve-tag
  arrow-with-text arrow-with-text*)

(define-group graphical-contains-text-tag
  triangle-with-text)

(tm-define (graphics-incomplete? obj)
  (:require (tm-is? obj 'triangle-with-text))
  ;;(display* "incomplete? " obj " -> " (< (tm-arity obj) 3) "\n")
  (< (tm-arity obj) 3))

(tm-define (graphics-complete? obj)
  (:require (tm-is? obj 'triangle-with-text))
  ;;(display* "complete? " obj " -> " (>= (tm-arity obj) 3) "\n")
  (>= (tm-arity obj) 3))

(tm-define (graphics-complete obj)
  (:require (tm-is? obj 'triangle-with-text))
  (if (> (tm-arity obj) 3)
      (list obj #f)
      (list (append obj (list '(text-at "X"))) (list 3 2 0))))

(define-graphics (triangle-with-text P1 P2 P3 T)
  ;;(display* "twt " P1 ", " P2 ", " P3 ", " T "\n")
  (let* ((p1 (if (tm-point? P1) P1 '(point "0" "0")))
         (p2 (if (tm-point? P2) P2 p1))
         (p3 (if (tm-point? P3) P3 p2))
         (t  (if (tm-is? T 'text-at) T '(text-at "X")))
         (z1 (point->complex p1))
         (z2 (point->complex p2))
         (z3 (point->complex p3))
         (p  (complex->point (/ (+ z1 z2 z3) 3))))
    `(superpose
       (cline ,p1 ,p2 ,p3)
       (with "text-at-halign" "center" "text-at-valign" "center"
         (text-at ,(tm-ref t 0) ,p)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Arrow or line with text
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-graphics (arrow-with-text P1 P2 T)
  (arrow-with-text-sub P1 P2 T 1.0))

(define-graphics (arrow-with-text* P1 P2 T)
  (arrow-with-text-sub P1 P2 T -1.0))

(define-group graphical-contains-curve-tag
  arrow-with-text arrow-with-text*)

(define-group graphical-contains-text-tag
  arrow-with-text arrow-with-text*)

(define-group variant-tag
  (arrow-with-text-tag))

(define-group arrow-with-text-tag
  arrow-with-text arrow-with-text*)

(tm-define (graphics-incomplete? obj)
  (:require (tm-in? obj '(arrow-with-text arrow-with-text*)))
  (< (tm-arity obj) 2))

(tm-define (graphics-complete? obj)
  (:require (tm-in? obj '(arrow-with-text arrow-with-text*)))
  (>= (tm-arity obj) 2))

(tm-define (graphics-complete obj)
  (:require (tm-in? obj '(arrow-with-text arrow-with-text*)))
  (if (> (tm-arity obj) 2)
      (list obj #f)
      (list (append obj (list '(math-at "x"))) (list 2 2 0))))

(define (directional-halign u)
  (cond ((> (real-part u) (*  0.333 (abs (imag-part u)))) "left")
	((< (real-part u) (* -0.333 (abs (imag-part u)))) "right")
	(else "center")))

(define (directional-valign u)
  (cond ((> (imag-part u) (*  0.666 (abs (real-part u)))) "bottom")
	((< (imag-part u) (* -0.666 (abs (real-part u)))) "top")
	(else "center")))

(define (arrow-with-text-sub P1 P2 T dir)
  ;;(display* "awt " P1 ", " P2 ", " T "\n")
  (let* ((p1 (if (tm-point? P1) P1 '(point "0" "0")))
         (p2 (if (tm-point? P2) P2 p1))
         (t  (if (tm-is? T 'math-at) T '(math-at "x")))
         (z1 (point->complex p1))
         (z2 (point->complex p2))
	 (m  (/ (+ z1 z2) 2))
	 (a  (magnitude (- z2 z1)))
	 (u  (if (= a 0) a (/ (- z2 z1) a)))
	 (n  (* u (make-rectangular 0.0 (* 0.1 dir))))
	 (c  (+ m n))
	 (ha (directional-halign n))
	 (va (directional-valign n)))
    `(superpose
       (line ,p1 ,p2)
       (with "text-at-halign" ,ha "text-at-valign" ,va
         (math-at (small ,(tm-ref t 0)) ,(complex->point c))))))

(tm-define (kbd-remove t forwards?)
  (:require (and (tree-in? t '(arrow-with-text arrow-with-text*))
                 (tree-down t)
                 (== (tree-index (tree-down t)) 2)
                 (tree-func? (tree-down t) 'math-at 1)
                 (tree-empty? (tree-ref t 2 0))))
  (tree-set t `(line ,(tree-ref t 0) ,(tree-ref t 1))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Native commutative diagrams
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; A diagram is stored as ordinary native graphics containing cd-vertex and
;; cd-arrow objects.  The arguments remain in the document tree; this function
;; only expands them into drawing primitives for the typesetter.

(tm-define (cd-graphics-render WIDTH HEIGHT BODY)
  (:secure #t)
  (let ((body (if (tm-is? BODY 'document) (tm-children BODY) (list BODY))))
    `(with "gr-mode" (tuple "edit" "cd-vertex")
           "gr-frame" (tuple "scale" "1cm" (tuple "0.5gw" "0.5gh"))
           "gr-geometry" (tuple "geometry" ,WIDTH ,HEIGHT "center")
           "gr-grid" (tuple "empty")
           "gr-grid-old" (tuple "cartesian" (point "0" "0") "1")
           "gr-edit-grid-aspect"
             (tuple (tuple "axes" "none") (tuple "1" "none")
                    (tuple "2" "#e0e0ff"))
           "gr-auto-crop" "false"
       (graphics "" ,@body))))

(define (cd-html-escape s)
  (let ((s (string-replace s "&" "&amp;")))
    (set! s (string-replace s "<" "&lt;"))
    (set! s (string-replace s ">" "&gt;"))
    (set! s (string-replace s "\"" "&quot;"))
    s))

(define (cd-html-label x)
  (let ((x (if (tm-is? x 'math-at) (tm-ref x 0) x)))
    (cd-html-escape
      (if (tm-atomic? x) (tm->string x)
          (tree->string (stree->tree x))))))

(define (cd-body-children body)
  (if (tm-is? body 'document) (tm-children body) (list body)))

(define (cd-html-point p height)
  (list (* 80.0 (tm->number (tm-x p)))
        (- height (* 80.0 (tm->number (tm-y p))))))

(define (cd-html-path p1 p2 opts height)
  (let* ((ends (cd-arrow-endpoints p1 p2 opts))
         (p1 (car ends)) (p2 (cadr ends))
         (a (cd-html-point p1 height)) (b (cd-html-point p2 height))
         (x1 (car a)) (y1 (cadr a)) (x2 (car b)) (y2 (cadr b))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (shape (cd-option opts "shape" "bezier"))
         (curve (* 80.0 (cd-number opts "curve" 0.0)))
         (cx (* curve (/ (- dy) len))) (cy (* curve (/ dx len))))
    (cond ((== shape "loop")
           (let* ((radius (* 80.0 (cd-number opts "loop-radius" 0.8)))
                  (angle (* 0.0174532925199433
                            (cd-number opts "loop-angle" 90.0)))
                  (ux (cos angle)) (uy (- (sin angle)))
                  (nx (- uy)) (ny ux))
             (string-append
               "M " (number->string x1) " " (number->string y1) " C "
               (number->string (+ x1 (* radius ux) (* radius nx))) " "
               (number->string (+ y1 (* radius uy) (* radius ny))) ", "
               (number->string (+ x1 (* radius ux) (* radius (- nx)))) " "
               (number->string (+ y1 (* radius uy) (* radius (- ny)))) ", "
               (number->string x1) " " (number->string y1))))
          ((= curve 0.0)
           (string-append "M " (number->string x1) " " (number->string y1)
                          " L " (number->string x2) " " (number->string y2)))
          (else (string-append
          "M " (number->string x1) " " (number->string y1) " C "
          (number->string (+ x1 (/ dx 3.0) cx)) " "
          (number->string (+ y1 (/ dy 3.0) cy)) ", "
          (number->string (+ x1 (* 2.0 (/ dx 3.0)) cx)) " "
          (number->string (+ y1 (* 2.0 (/ dy 3.0)) cy)) ", "
          (number->string x2) " " (number->string y2))))))

(define (cd-html-arrow x height)
  (let* ((p1 (tm-ref x 0)) (p2 (tm-ref x 1)) (label (tm-ref x 2))
         (opts (tm-ref x 3)) (a (cd-html-point p1 height))
         (b (cd-html-point p2 height))
         (mx (/ (+ (car a) (car b)) 2.0))
         (my (- (/ (+ (cadr a) (cadr b)) 2.0) 9.0))
         (color (cd-html-escape (cd-option opts "color" "black")))
         (label-color
           (cd-html-escape (cd-option opts "label-color" "black")))
         (body (cd-option opts "body" "solid"))
         (head (cd-option opts "head" "arrowhead"))
         (tail (cd-option opts "tail" "none"))
         (dash (cond ((== body "dashed") "8 6")
                     ((== body "dotted") "2 5") (else "none"))))
    (string-append
      "<path d=\"" (cd-html-path p1 p2 opts height)
      "\" fill=\"none\" stroke=\"" color
      "\" stroke-width=\"2\" stroke-dasharray=\"" dash "\""
      (if (== tail "none") ""
          " marker-start=\"url(#athena-cd-tail)\"")
      (if (in? head '("none" "corner" "corner-inverse")) ""
          " marker-end=\"url(#athena-cd-head)\"")
      "/>"
      "<text x=\"" (number->string mx) "\" y=\"" (number->string my)
      "\" text-anchor=\"middle\" fill=\"" label-color
      "\" font-family=\"serif\">" (cd-html-label label) "</text>")))

(define (cd-html-vertex x height)
  (let* ((p (cd-html-point (tm-ref x 0) height))
         (label (tm-ref x 1)))
    (string-append
      "<text x=\"" (number->string (car p)) "\" y=\""
      (number->string (+ (cadr p) 5.0))
      "\" text-anchor=\"middle\" font-family=\"serif\" font-size=\"18\">"
      (cd-html-label label) "</text>")))

(tm-define (cd-graphics-html-source WIDTH HEIGHT BODY)
  (:secure #t)
  (let* ((w (* 80.0 (or (string->number (tm->string WIDTH)) 8.1)))
         (h (* 80.0 (or (string->number (tm->string HEIGHT)) 3.1)))
         (children (cd-body-children BODY))
         (arrows (list-filter children (cut tm-is? <> 'cd-arrow)))
         (vertices (list-filter children (cut tm-is? <> 'cd-vertex))))
    (string-append
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
      (number->string w) " " (number->string h)
      "\" style=\"width:100%;max-width:" (number->string w)
      "px;overflow:visible\" role=\"img\" aria-label=\"Commutative diagram\">"
      "<defs><marker id=\"athena-cd-head\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\"><path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"context-stroke\"/></marker><marker id=\"athena-cd-tail\" viewBox=\"0 0 10 10\" refX=\"1\" refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\"><path d=\"M 10 0 L 0 5 L 10 10\" fill=\"none\" stroke=\"context-stroke\"/></marker></defs>"
      (string-concatenate (map (cut cd-html-arrow <> h) arrows))
      (string-concatenate (map (cut cd-html-vertex <> h) vertices))
      "</svg>")))

(define (cd-html-wrap-attrs attrs body)
  (if (null? attrs) body
      `(html-attr ,(caar attrs) ,(cdar attrs)
         ,(cd-html-wrap-attrs (cdr attrs) body))))

(define (cd-html-element tag attrs body)
  (cd-html-wrap-attrs attrs `(html-tag ,tag ,body)))

(define (cd-html-arrow-tree x height)
  (let* ((p1 (tm-ref x 0)) (p2 (tm-ref x 1)) (label (tm-ref x 2))
         (opts (tm-ref x 3)) (ends (cd-arrow-endpoints p1 p2 opts))
         (a (cd-html-point (car ends) height))
         (b (cd-html-point (cadr ends) height))
         (pos (cd-number opts "label-position" 0.5))
         (side (cd-option opts "label-alignment" "left"))
         (gap (cond ((== side "right") 12.0)
                    ((== side "over") 0.0) (else -12.0)))
         (mx (+ (car a) (* pos (- (car b) (car a)))))
         (my (+ (cadr a) (* pos (- (cadr b) (cadr a))) gap))
         (body (cd-option opts "body" "solid"))
         (head (cd-option opts "head" "arrowhead"))
         (tail (cd-option opts "tail" "none"))
         (attrs `(("d" . ,(cd-html-path p1 p2 opts height))
                  ("fill" . "none")
                  ("stroke" . ,(if (== body "none") "none"
                                    (cd-option opts "color" "black")))
                  ("stroke-width" . "2")
                  ("stroke-dasharray" .
                    ,(cond ((== body "dashed") "8 6")
                           ((== body "dotted") "2 5") (else "none"))))))
    (when (!= tail "none")
      (set! attrs (cons '("marker-start" . "url(#athena-cd-tail)") attrs)))
    (when (nin? head '("none" "corner" "corner-inverse"))
      (set! attrs (cons '("marker-end" . "url(#athena-cd-head)") attrs)))
    (list
      (cd-html-element "path" attrs "")
      (cd-html-element "text"
        `(("x" . ,(number->string mx)) ("y" . ,(number->string my))
          ("text-anchor" . "middle")
          ("fill" . ,(cd-option opts "label-color" "black"))
          ("font-family" . "serif"))
        (cd-html-label label)))))

(define (cd-html-vertex-tree x height)
  (let ((p (cd-html-point (tm-ref x 0) height)))
    (cd-html-element "text"
      `(("x" . ,(number->string (car p)))
        ("y" . ,(number->string (+ (cadr p) 5.0)))
        ("text-anchor" . "middle") ("font-family" . "serif")
        ("font-size" . "18"))
      (cd-html-label (tm-ref x 1)))))

(tm-define (cd-graphics-html-tree WIDTH HEIGHT BODY)
  (:secure #t)
  (let* ((w (* 80.0 (or (string->number (tm->string WIDTH)) 8.1)))
         (h (* 80.0 (or (string->number (tm->string HEIGHT)) 3.1)))
         (children (cd-body-children BODY))
         (arrows (list-filter children (cut tm-is? <> 'cd-arrow)))
         (vertices (list-filter children (cut tm-is? <> 'cd-vertex)))
         (head-path
           (cd-html-element "path"
             '(("d" . "M 0 0 L 10 5 L 0 10 z")
               ("fill" . "context-stroke")) ""))
         (tail-path
           (cd-html-element "path"
             '(("d" . "M 10 0 L 0 5 L 10 10") ("fill" . "none")
               ("stroke" . "context-stroke")) ""))
         (head-marker
           (cd-html-element "marker"
             '(("id" . "athena-cd-head") ("viewBox" . "0 0 10 10")
               ("refX" . "9") ("refY" . "5") ("markerWidth" . "7")
               ("markerHeight" . "7") ("orient" . "auto-start-reverse"))
             head-path))
         (tail-marker
           (cd-html-element "marker"
             '(("id" . "athena-cd-tail") ("viewBox" . "0 0 10 10")
               ("refX" . "1") ("refY" . "5") ("markerWidth" . "7")
               ("markerHeight" . "7") ("orient" . "auto-start-reverse"))
             tail-path))
         (defs (cd-html-element "defs" '()
                 `(concat ,head-marker ,tail-marker)))
         (objects
           (append (append-map (cut cd-html-arrow-tree <> h) arrows)
                   (map (cut cd-html-vertex-tree <> h) vertices))))
    (cd-html-element "svg"
      `(("xmlns" . "http://www.w3.org/2000/svg")
        ("viewBox" . ,(string-append "0 0 " (number->string w) " "
                                     (number->string h)))
        ("style" . ,(string-append "width:100%;max-width:"
                                   (number->string w)
                                   "px;overflow:visible"))
        ("role" . "img") ("aria-label" . "Commutative diagram"))
      `(concat ,defs ,@objects))))

(define (cd-option opts key fallback)
  (if (not (tm-func? opts 'tuple)) fallback
      (let loop ((i 0))
        (cond ((>= (+ i 1) (tm-arity opts)) fallback)
              ((== (tm->string (tm-ref opts i)) key)
               (tm->string (tm-ref opts (+ i 1))))
              (else (loop (+ i 2)))))))

(define (cd-number opts key fallback)
  (or (string->number (cd-option opts key "")) fallback))

(define (cd-point+ p dx dy)
  `(point ,(number->string (+ (tm->number (tm-x p)) dx))
          ,(number->string (+ (tm->number (tm-y p)) dy))))

(define (cd-arrowhead style)
  (cond ((in? style '("none" "corner" "corner-inverse")) "")
        ((in? style '("epi" "double")) "<gtr><gtr>")
        ((in? style '("harpoon-top" "harpoon-bottom")) "|<gtr>")
        (else "<gtr>")))

(define (cd-arrowtail style)
  (cond ((in? style '("mono" "reverse")) "<less>")
        ((== style "maps-to") "|")
        ((in? style '("hook-top" "hook-bottom")) "o")
        (else "")))

(define (cd-body-properties body level)
  (let* ((dash (cond ((== body "dashed") "3ln 3ln")
                     ((== body "dotted") "1ln 2ln")
                     (else "default")))
         (width (number->string (+ 1.0 (* 0.35 (- level 1))))))
    (list dash (string-append width "ln"))))

(define (cd-arrow-endpoints p1 p2 opts)
  (let* ((x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (ux (/ dx len)) (uy (/ dy len))
         (nx (- uy)) (ny ux)
         (offset (cd-number opts "offset" 0.0))
         (s1 (cd-number opts "shorten-source" 0.0))
         (s2 (cd-number opts "shorten-target" 0.0)))
    (list (cd-point+ p1 (+ (* nx offset) (* ux s1))
                          (+ (* ny offset) (* uy s1)))
          (cd-point+ p2 (+ (* nx offset) (* ux (- s2)))
                          (+ (* ny offset) (* uy (- s2)))))))

(define (cd-midpoint p1 p2)
  `(point ,(number->string (/ (+ (tm->number (tm-x p1))
                                 (tm->number (tm-x p2))) 2.0))
          ,(number->string (/ (+ (tm->number (tm-y p1))
                                 (tm->number (tm-y p2))) 2.0))))

(define (cd-bar-at p1 p2 pos)
  (let* ((x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (mx (+ x1 (* pos dx))) (my (+ y1 (* pos dy)))
         (nx (* 0.09 (/ (- dy) len))) (ny (* 0.09 (/ dx len))))
    `(line (point ,(number->string (- mx nx)) ,(number->string (- my ny)))
           (point ,(number->string (+ mx nx)) ,(number->string (+ my ny))))))

(define (cd-squiggle p1 p2)
  (let* ((x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (nx (/ (- dy) len)) (ny (/ dx len)))
    `(spline
      ,@(map (lambda (i)
               (let* ((u (/ i 12.0))
                      (wave (* 0.06 (sin (* u 12.0 3.141592653589793)))))
                 `(point ,(number->string (+ x1 (* u dx) (* wave nx)))
                         ,(number->string (+ y1 (* u dy) (* wave ny))))))
             (.. 0 13)))))

(define (cd-arrow-curve p1 p2 opts)
  (let* ((shape (cd-option opts "shape" "bezier"))
         (curve (cd-number opts "curve" 0.0))
         (x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (nx (/ (- dy) len)) (ny (/ dx len))
         (cx (* nx curve)) (cy (* ny curve)))
    (cond ((== shape "loop")
           (let* ((radius (cd-number opts "loop-radius" 0.8))
                  (angle (* 0.0174532925199433
                            (cd-number opts "loop-angle" 90.0)))
                  (ux (cos angle)) (uy (sin angle))
                  (nx (- uy)) (ny ux))
             `(bezier ,p1
                      ,(cd-point+ p1 (+ (* radius ux) (* radius nx))
                                    (+ (* radius uy) (* radius ny)))
                      ,(cd-point+ p1 (+ (* radius ux) (* radius (- nx)))
                                    (+ (* radius uy) (* radius (- ny))))
                      ,p1)))
          ((or (== shape "arc") (!= curve 0.0))
           `(bezier ,p1
                    ,(cd-point+ p1 (+ (/ dx 3.0) cx) (+ (/ dy 3.0) cy))
                    ,(cd-point+ p1 (+ (* 2.0 (/ dx 3.0)) cx)
                                  (+ (* 2.0 (/ dy 3.0)) cy))
                    ,p2))
          (else `(line ,p1 ,p2)))))

(define (cd-offset-curve p1 p2 opts delta)
  (let* ((x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (len (max 0.001 (sqrt (+ (* (- x2 x1) (- x2 x1))
                                  (* (- y2 y1) (- y2 y1))))))
         (ox (* delta (/ (- y2 y1) len)))
         (oy (* delta (/ (- x1 x2) len))))
    (cd-arrow-curve (cd-point+ p1 ox oy) (cd-point+ p2 ox oy) opts)))

(define (cd-arrow-label p1 p2 label opts)
  (let* ((pos (cd-number opts "label-position" 0.5))
         (curve (cd-number opts "curve" 0.0))
         (side (cd-option opts "label-alignment" "left"))
         (sign (if (== side "right") -1.0 1.0))
         (x1 (tm->number (tm-x p1))) (y1 (tm->number (tm-y p1)))
         (x2 (tm->number (tm-x p2))) (y2 (tm->number (tm-y p2)))
         (dx (- x2 x1)) (dy (- y2 y1))
         (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
         (gap (if (== side "over") 0.0 (* sign (+ 0.13 (* 0.35 curve)))))
         (x (+ x1 (* pos dx) (* gap (/ (- dy) len))))
         (y (+ y1 (* pos dy) (* gap (/ dx len))))
         (color (cd-option opts "label-color" "black")))
    `(with "color" ,color "text-at-halign" "center" "text-at-valign" "center"
       (math-at (small ,label) (point ,(number->string x) ,(number->string y))))))

(define-graphics (cd-vertex P T ID)
  (let ((p (if (tm-point? P) P '(point "0" "0")))
        (t (if (tm-is? T 'math-at) (tm-ref T 0) T)))
    `(with "gid" ,ID "text-at-halign" "center" "text-at-valign" "center"
       (math-at ,t ,p))))

(define-graphics (cd-arrow P1 P2 T OPTS)
  (let* ((p1 (if (tm-point? P1) P1 '(point "0" "0")))
         (p2 (if (tm-point? P2) P2 p1))
         (label (if (tm-is? T 'math-at) (tm-ref T 0) T))
         (opts (if (tm-func? OPTS 'tuple) OPTS '(tuple)))
         (ends (cd-arrow-endpoints p1 p2 opts))
         (q1 (car ends)) (q2 (cadr ends))
         (body (cd-option opts "body" "solid"))
         (tail (cd-option opts "tail" "none"))
         (head (cd-option opts "head" "arrowhead"))
         (color (cd-option opts "color" "black"))
         (level (max 1 (min 4 (inexact->exact
                                (round (cd-number opts "level" 1))))))
         (props (cd-body-properties body level))
         (curve (if (== body "squiggly") (cd-squiggle q1 q2)
                    (cd-arrow-curve q1 q2 opts)))
         (main `(with "color" ,color "line-dash" ,(car props)
                       "line-width" ,(cadr props)
                       "arrow-begin" ,(cd-arrowtail tail)
                       "arrow-end" ,(cd-arrowhead head) ,curve))
         (body-drawing
          (cond ((== body "none")
                 `(superpose (with "point-style" "none" ,q1)
                             (with "point-style" "none" ,q2)))
                ((in? body '("double" "double-barred"))
                 `(superpose
                    (with "color" ,color "line-width" ,(cadr props)
                      ,(cd-offset-curve q1 q2 opts -0.045))
                    (with "color" ,color "line-width" ,(cadr props)
                      ,(cd-offset-curve q1 q2 opts 0.045))
                    ,main))
                (else main))))
    `(superpose
       ,body-drawing
       ,@(if (in? body '("barred" "double-barred"))
             (if (== body "double-barred")
                 (list (cd-bar-at q1 q2 0.46) (cd-bar-at q1 q2 0.54))
                 (list (cd-bar-at q1 q2 0.5)))
             '())
       ,@(if (in? body '("bullet" "hollow-bullet"))
             (list `(with "point-style" "round" "point-size" "5ln"
                          "fill-color" ,(if (== body "bullet") color "white")
                          "color" ,color ,(cd-midpoint q1 q2)))
             '())
       ,(cd-arrow-label q1 q2 label opts))))

(define-group graphical-contains-curve-tag cd-arrow)
(define-group graphical-contains-text-tag cd-vertex cd-arrow)

(tm-define (graphics-incomplete? obj)
  (:require (tm-in? obj '(cd-vertex cd-arrow)))
  (< (tm-arity obj) 2))

(tm-define (graphics-complete? obj)
  (:require (tm-in? obj '(cd-vertex cd-arrow)))
  (>= (tm-arity obj) 2))

(tm-define (graphics-complete obj)
  (:require (tm-is? obj 'cd-vertex))
  (list `(cd-vertex ,(tm-ref obj 0) (math-at "X")
                    ,(string-append "vertex-" (number->string (texmacs-time))))
        (list 1 2 0)))

(tm-define (graphics-complete obj)
  (:require (tm-is? obj 'cd-arrow))
  (list `(cd-arrow ,(tm-ref obj 0) ,(tm-ref obj 1) (math-at "")
                   (tuple "shape" "bezier" "curve" "0"
                          "offset" "0" "shorten-source" "0"
                          "shorten-target" "0" "level" "1"
                          "label-alignment" "left" "label-position" "0.5"
                          "color" "black" "label-color" "black"
                          "tail" "none" "body" "solid"
                          "head" "arrowhead" "edge-type" "arrow"
                          "loop-radius" "0.8" "loop-angle" "90"
                          "source-alignment" "centre"
                          "target-alignment" "centre"))
        #f))
