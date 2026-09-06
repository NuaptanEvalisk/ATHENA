;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : commutative-diagram.scm
;; DESCRIPTION : native ATHENA commutative-diagram AST and editor
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena commutative-diagram)
  (:use (kernel library tree)
        (kernel athena tm-define)))

;; Source representation:
;;   (commutative-diagram width height
;;     (cd-body ""
;;       (cd-vertex id x y formula)
;;       (cd-arrow id source-id target-id formula options)))
;; Coordinates are expressed in centimetres around the diagram centre.

(define cd-id-counter 0)

;; Editing state is session-local. Only vertices and arrows live in the AST.
(define cd-session-body #f)
(define cd-selected-kind #f)
(define cd-selected-id #f)
(define cd-hover-kind #f)
(define cd-hover-id #f)
(define cd-halos-visible? #f)
(define cd-context-kind #f)
(define cd-context-time 0)
(define cd-interaction 'idle)
(define cd-press-point #f)
(define cd-interaction-id #f)
(define cd-interaction-part #f)
(define cd-drag-point #f)
(define cd-drag-target #f)
(define cd-drag-loop-angle #f)

(define cd-vertex-content-radius 0.24)
(define cd-vertex-move-radius 0.46)
(define cd-arrow-hit-radius 0.38)
(define cd-handle-hit-radius 0.24)
(define cd-drag-threshold 0.14)

(define (cd-path-inside? p label)
  (and (pair? p)
       (let ((t (path->tree p)))
         (or (and (tree? t) (== (tree-label t) label))
             (cd-path-inside? (cDr p) label)))))

(tm-define (in-commutative-diagram?)
  (cd-path-inside? (cDr (cursor-path)) 'commutative-diagram))

(define (cd-current-diagram)
  (tree-innermost 'commutative-diagram #t))

(tm-define (commutative-diagram-show-hidden)
  (:interactive #t)
  (and-with diagram (cd-current-diagram)
    (tree-select diagram)
    (inactive-toggle diagram)))

(tm-define (commutative-diagram-describe)
  (:interactive #t)
  (and-with diagram (cd-current-diagram)
    ;; Contextual help evaluates focus-tree while constructing the help page.
    (tree-select diagram)
    (focus-help)))

(define (cd-string t)
  (if (tm-atomic? t) (tm->string t) ""))

(define (cd-number t fallback)
  (or (string->number (cd-string t)) fallback))

(define (cd-coordinate x fallback)
  (cond ((number? x) x)
        ((string? x) (or (string->number x) fallback))
        ((tm-atomic? x) (or (string->number (tm->string x)) fallback))
        (else fallback)))

(define (cd-new-id prefix)
  (set! cd-id-counter (+ cd-id-counter 1))
  (string-append prefix "-" (number->string (texmacs-time)) "-"
                 (number->string cd-id-counter)))

(define (cd-children body)
  (if (or (tm-is? body 'cd-body) (tm-is? body 'document))
      (tm-children body)
      (list body)))

(define (cd-normalize-body! body)
  (when (tm-is? body 'document)
    (let ((children (map tree-copy (tm-children body))))
      (tree-set! body `(cd-body "" ,@children))))
  body)

(define (cd-vertices body)
  (list-filter (cd-children body) (cut tm-is? <> 'cd-vertex)))

(define (cd-arrows body)
  (list-filter (cd-children body) (cut tm-is? <> 'cd-arrow)))

(define (cd-vertex-id v) (cd-string (tm-ref v 0)))
(define (cd-vertex-x v) (cd-number (tm-ref v 1) 0.0))
(define (cd-vertex-y v) (cd-number (tm-ref v 2) 0.0))
(define (cd-arrow-id a) (cd-string (tm-ref a 0)))
(define (cd-arrow-source a) (cd-string (tm-ref a 1)))
(define (cd-arrow-target a) (cd-string (tm-ref a 2)))

(define (cd-option a key fallback)
  (let ((opts (and (tm-is? a 'cd-arrow) (tm-ref a 4))))
    (if (not (tm-is? opts 'tuple)) fallback
        (let loop ((i 0))
          (cond ((>= (+ i 1) (tm-arity opts)) fallback)
                ((== (cd-string (tm-ref opts i)) key)
                 (cd-string (tm-ref opts (+ i 1))))
                (else (loop (+ i 2))))))))

(define (cd-option-number a key fallback)
  (or (string->number (cd-option a key "")) fallback))

(define (cd-default-arrow-options)
  '(tuple "edge-type" "arrow"
          "tail" "none" "body" "solid" "head" "arrowhead"
          "label-alignment" "left" "label-position" "50"
          "offset" "0" "curve" "0" "loop-radius" "3"
          "loop-angle" "0" "shorten-source" "0"
          "shorten-target" "0" "level" "1"
          "color" "black" "label-color" "black"))

(define (cd-find-vertex body id)
  (list-find (cd-vertices body) (lambda (v) (== (cd-vertex-id v) id))))

(define (cd-find-arrow body id)
  (list-find (cd-arrows body) (lambda (a) (== (cd-arrow-id a) id))))

(define (cd-distance x1 y1 x2 y2)
  (sqrt (+ (* (- x2 x1) (- x2 x1))
           (* (- y2 y1) (- y2 y1)))))

(define (cd-nearest-vertex body x y radius)
  (let ((best #f) (best-distance radius))
    (for (v (cd-vertices body))
      (let ((distance (cd-distance x y (cd-vertex-x v) (cd-vertex-y v))))
        (when (< distance best-distance)
          (set! best v)
          (set! best-distance distance))))
    best))

(define (cd-point-segment-distance x y x1 y1 x2 y2)
  (let* ((dx (- x2 x1)) (dy (- y2 y1))
         (length2 (+ (* dx dx) (* dy dy))))
    (if (< length2 0.000001) (cd-distance x y x1 y1)
        (let* ((u (max 0.0 (min 1.0
                         (/ (+ (* (- x x1) dx) (* (- y y1) dy)) length2))))
               (qx (+ x1 (* u dx))) (qy (+ y1 (* u dy))))
          (cd-distance x y qx qy)))))

(define (cd-point+ p q)
  (list (+ (car p) (car q)) (+ (cadr p) (cadr q))))

(define (cd-point* p k)
  (list (* (car p) k) (* (cadr p) k)))

(define (cd-bezier-point points t)
  (let* ((p0 (list-ref points 0)) (p1 (list-ref points 1))
         (p2 (list-ref points 2)) (p3 (list-ref points 3))
         (u (- 1.0 t)))
    (cd-point+
      (cd-point* p0 (* u u u))
      (cd-point+
        (cd-point* p1 (* 3.0 u u t))
        (cd-point+
          (cd-point* p2 (* 3.0 u t t))
          (cd-point* p3 (* t t t)))))))

(define (cd-bezier-tangent points t)
  (let* ((epsilon 0.001)
         (a (cd-bezier-point points (max 0.0 (- t epsilon))))
         (b (cd-bezier-point points (min 1.0 (+ t epsilon))))
         (dx (- (car b) (car a))) (dy (- (cadr b) (cadr a)))
         (len (max 0.000001 (sqrt (+ (* dx dx) (* dy dy))))))
    (list (/ dx len) (/ dy len))))

(define (cd-loop-geometry centre angle-degrees radius-setting)
  (let* ((angle (* 0.0174532925199433 angle-degrees))
         (radius (+ 0.58 (* 0.12 (abs radius-setting))))
         (sign (if (< radius-setting 0.0) -1.0 1.0))
         (u (list (cos angle) (sin angle)))
         (n (list (* sign (- (cadr u))) (* sign (car u))))
         (q1 (cd-point+ centre (cd-point* n -0.20)))
         (q2 (cd-point+ centre (cd-point* n 0.20)))
         (outer (cd-point+ centre (cd-point* u radius)))
         (c1 (cd-point+ outer (cd-point* n (* -0.72 radius))))
         (c2 (cd-point+ outer (cd-point* n (* 0.72 radius)))))
    (list q1 c1 c2 q2)))

(define (cd-arrow-geometry body a)
  (let* ((source (cd-find-vertex body (cd-arrow-source a)))
         (target (cd-find-vertex body (cd-arrow-target a))))
    (and source target
      (let* ((p1 (list (cd-vertex-x source) (cd-vertex-y source)))
             (p2 (list (cd-vertex-x target) (cd-vertex-y target)))
             (loop? (== (cd-arrow-source a) (cd-arrow-target a))))
        (if loop?
            (cd-loop-geometry p1
              (cd-option-number a "loop-angle" 0.0)
              (cd-option-number a "loop-radius" 3.0))
            (let* ((dx (- (car p2) (car p1))) (dy (- (cadr p2) (cadr p1)))
                   (len (max 0.001 (sqrt (+ (* dx dx) (* dy dy)))))
                   (u (list (/ dx len) (/ dy len)))
                   (n (list (- (cadr u)) (car u)))
                   (shift (* 0.08 (cd-option-number a "offset" 0.0)))
                   (curve (* 0.18 (cd-option-number a "curve" 0.0)))
                   (base-short 0.18)
                   (source-short (+ base-short
                     (* len 0.01 (cd-option-number a "shorten-source" 0.0))))
                   (target-short (+ base-short
                     (* len 0.01 (cd-option-number a "shorten-target" 0.0))))
                   (q1 (cd-point+ p1
                         (cd-point+ (cd-point* u source-short)
                                    (cd-point* n shift))))
                   (q2 (cd-point+ p2
                         (cd-point+ (cd-point* u (- target-short))
                                    (cd-point* n shift))))
                   (delta (list (- (car q2) (car q1))
                                (- (cadr q2) (cadr q1))))
                   (bend (cd-point* n curve))
                   (c1 (cd-point+ q1
                         (cd-point+ (cd-point* delta (/ 1.0 3.0)) bend)))
                   (c2 (cd-point+ q1
                         (cd-point+ (cd-point* delta (/ 2.0 3.0)) bend))))
              (list q1 c1 c2 q2)))))))

(define (cd-arrow-samples body a)
  (and-with geometry (cd-arrow-geometry body a)
    (map (lambda (i) (cd-bezier-point geometry (/ i 24.0))) (.. 0 25))))

(define (cd-arrow-endpoints body a)
  (and-with geometry (cd-arrow-geometry body a)
    (list (car geometry) (list-ref geometry 3))))

(define (cd-nearest-arrow body x y radius)
  (let ((best #f) (best-distance radius))
    (for (a (cd-arrows body))
      (and-with samples (cd-arrow-samples body a)
        (let loop ((points samples))
          (when (pair? (cdr points))
            (let* ((p1 (car points)) (p2 (cadr points))
                   (distance (cd-point-segment-distance x y
                     (car p1) (cadr p1) (car p2) (cadr p2))))
              (when (< distance best-distance)
                (set! best a)
                (set! best-distance distance))
              (loop (cdr points)))))))
    best))

(define (cd-selected? body kind id)
  (and (== cd-selected-kind kind) (== cd-selected-id id)
       (if (== kind 'vertex) (cd-find-vertex body id)
           (cd-find-arrow body id))))

(define (cd-hovered? body kind id)
  (and (== cd-hover-kind kind) (== cd-hover-id id)
       (if (== kind 'vertex) (cd-find-vertex body id)
           (cd-find-arrow body id))))

(define (cd-layout-point p)
  `(tuple ,(number->string (car p)) ,(number->string (cadr p))))

(define (cd-state-kind-string kind)
  (if (symbol? kind) (symbol->string kind) ""))

(define (cd-layout-arrows body)
  (let loop ((arrows (cd-arrows body)) (result '()))
    (if (null? arrows) (reverse result)
        (let* ((arrow (car arrows))
               (geometry (cd-arrow-geometry body arrow)))
          (loop (cdr arrows)
                (if geometry
                    (cons `(tuple "arrow" ,(cd-arrow-id arrow)
                                  ,@(map cd-layout-point geometry))
                          result)
                    result))))))

(define (cd-layout-drag body)
  (and cd-drag-point
       (cond
         ((== cd-interaction 'connecting)
          (and-with source (cd-find-vertex body cd-interaction-id)
            (let ((source-point
                    (list (cd-vertex-x source) (cd-vertex-y source))))
              (if (== cd-drag-target cd-interaction-id)
                  `(tuple "drag-curve"
                          ,@(map cd-layout-point
                            (cd-loop-geometry source-point
                              (or cd-drag-loop-angle 0.0) 3.0)))
                  `(tuple "drag" ,(cd-layout-point source-point)
                                  ,(cd-layout-point cd-drag-point))))))
         ((== cd-interaction 'reconnecting)
          (and-with arrow (cd-find-arrow body cd-interaction-id)
            (and-with ends (cd-arrow-endpoints body arrow)
              (let* ((fixed-id
                       (if (== cd-interaction-part 'source)
                           (cd-arrow-target arrow) (cd-arrow-source arrow)))
                     (fixed-vertex (cd-find-vertex body fixed-id))
                     (fixed (if (== cd-interaction-part 'source)
                                (cadr ends) (car ends))))
                (if (and fixed-vertex (== cd-drag-target fixed-id))
                    `(tuple "drag-curve"
                            ,@(map cd-layout-point
                              (cd-loop-geometry
                                (list (cd-vertex-x fixed-vertex)
                                      (cd-vertex-y fixed-vertex))
                                (or cd-drag-loop-angle
                                    (cd-option-number arrow "loop-angle" 0.0))
                                (cd-option-number arrow "loop-radius" 3.0))))
                    `(tuple "drag"
                            ,(cd-layout-point
                               (if (== cd-interaction-part 'source)
                                   cd-drag-point fixed))
                            ,(cd-layout-point
                               (if (== cd-interaction-part 'source)
                                   fixed cd-drag-point))))))))
         (else #f))))

(tm-define (commutative-diagram-layout BODY)
  (:secure #t)
  (if (not (tree? BODY)) '(tuple)
      (let* ((selected?
               (and cd-halos-visible? cd-selected-kind cd-selected-id
                    (cd-selected? BODY cd-selected-kind cd-selected-id)))
             (hover?
               (and cd-halos-visible? cd-hover-kind cd-hover-id
                    (cd-hovered? BODY cd-hover-kind cd-hover-id)))
             (drag (cd-layout-drag BODY)))
        `(tuple
           (tuple "selected"
                  ,(if selected?
                       (cd-state-kind-string cd-selected-kind) "")
                  ,(if selected? cd-selected-id ""))
           (tuple "hover"
                  ,(if hover? (cd-state-kind-string cd-hover-kind) "")
                  ,(if hover? cd-hover-id ""))
           (tuple "target" ,(or cd-drag-target ""))
           ,@(cd-layout-arrows BODY)
           ,@(if drag (list drag) '())))))

(define (cd-snap x)
  (/ (round (* x 2.0)) 2.0))

(define (cd-snap-point p)
  (list (cd-snap (car p)) (cd-snap (cadr p))))

(define (cd-add-vertex body x y)
  (let ((id (cd-new-id "vertex")))
    (tree-insert! body (tree-arity body)
                  `((cd-vertex ,id ,(number->string x) ,(number->string y)
                               (math "X"))))
    (tree-ref body (- (tree-arity body) 1))))

(define (cd-add-arrow body source target)
  (tree-insert! body (tree-arity body)
    `((cd-arrow ,(cd-new-id "arrow") ,source ,target (math "")
                ,(cd-default-arrow-options))))
  (tree-ref body (- (tree-arity body) 1)))

(define (cd-reset-interaction)
  (set! cd-interaction 'idle)
  (set! cd-press-point #f)
  (set! cd-interaction-id #f)
  (set! cd-interaction-part #f)
  (set! cd-drag-point #f)
  (set! cd-drag-target #f)
  (set! cd-drag-loop-angle #f))

(define (cd-begin-session body)
  (cd-normalize-body! body)
  (let ((p (tree->path body)))
    (set! cd-halos-visible? #t)
    (when (not (== p cd-session-body))
      (set! cd-session-body p)
      (set! cd-selected-kind #f)
      (set! cd-selected-id #f)
      (set! cd-hover-kind #f)
      (set! cd-hover-id #f)
      (cd-reset-interaction))))

(define (cd-select kind id)
  (let ((changed? (or (!= kind cd-selected-kind) (!= id cd-selected-id))))
    (set! cd-selected-kind kind)
    (set! cd-selected-id id)
    changed?))

(define (cd-set-hover kind id)
  (let ((changed? (or (!= kind cd-hover-kind) (!= id cd-hover-id))))
    (set! cd-hover-kind kind)
    (set! cd-hover-id id)
    changed?))

(define (cd-refresh)
  (when cd-session-body
    (let ((body (path->tree cd-session-body)))
      (when (and (tree? body) (tm-is? body 'cd-body))
        (update-path cd-session-body)))))

(define (cd-cursor-in-session?)
  (and cd-session-body
       (and-with body (tree-innermost 'cd-body #t)
         (== (tree->path body) cd-session-body))))

(tm-define (notify-cursor-moved status)
  (:require cd-session-body)
  (let ((visible? (cd-cursor-in-session?)))
    (when (!= visible? cd-halos-visible?)
      (set! cd-halos-visible? visible?)
      (when (not visible?) (cd-set-hover #f #f))
      ;; Cursor notification can run before the diagram's pointer callback.
      ;; Retypesetting here would detach the BODY passed to that callback.
      (delayed (:idle 1) (cd-refresh))))
  (former status))

(define (cd-arrow-handle-at body x y)
  (and (== cd-selected-kind 'arrow)
       (and-with a (cd-find-arrow body cd-selected-id)
         (and-with ends (cd-arrow-endpoints body a)
           (cond ((< (cd-distance x y (car (car ends)) (cadr (car ends)))
                     cd-handle-hit-radius) (cons a 'source))
                 ((< (cd-distance x y (car (cadr ends)) (cadr (cadr ends)))
                     cd-handle-hit-radius) (cons a 'target))
                 (else #f))))))

(define (cd-hit-test body x y)
  (or (and-with handle (cd-arrow-handle-at body x y)
        (list 'handle (car handle) (cdr handle)))
      (and-with v (cd-nearest-vertex body x y cd-vertex-content-radius)
        (list 'vertex-content v))
      (and-with a (cd-nearest-arrow body x y cd-arrow-hit-radius)
        (list 'arrow a))
      (and-with v (cd-nearest-vertex body x y cd-vertex-move-radius)
        (list 'vertex-move v))
      (list 'empty)))

(define (cd-hit-kind hit) (car hit))
(define (cd-hit-object hit) (and (> (length hit) 1) (cadr hit)))

(define (cd-focus-object object index)
  (delayed (:idle 1) (tree-go-to object index :end)))

(define (cd-focus-navigation body)
  (delayed (:idle 1) (tree-go-to body 0 :end)))

(define (cd-handle-press body x y)
  (cd-begin-session body)
  (let* ((hit (cd-hit-test body x y)) (kind (cd-hit-kind hit))
         (object (cd-hit-object hit))
         (old-kind cd-selected-kind) (old-id cd-selected-id))
    (set! cd-press-point (list x y))
    (cond
      ((== kind 'handle)
       (cd-select 'arrow (cd-arrow-id object))
       (cd-focus-navigation body)
       (set! cd-interaction 'pending-reconnect)
       (set! cd-interaction-id (cd-arrow-id object))
       (set! cd-interaction-part (caddr hit)))
      ((== kind 'vertex-content)
       (cd-select 'vertex (cd-vertex-id object))
       (when (not (and (== old-kind 'vertex)
                       (== old-id (cd-vertex-id object))))
         (cd-focus-navigation body))
       (set! cd-interaction 'pending-connect)
       (set! cd-interaction-id (cd-vertex-id object))
       (when (and (== old-kind 'vertex)
                  (== old-id (cd-vertex-id object)))
         (set! cd-interaction-part 'edit)))
      ((== kind 'vertex-move)
       (cd-select 'vertex (cd-vertex-id object))
       (cd-focus-navigation body)
       (set! cd-interaction 'moving)
       (set! cd-interaction-id (cd-vertex-id object)))
      ((== kind 'arrow)
       (cd-select 'arrow (cd-arrow-id object))
       (when (not (and (== old-kind 'arrow)
                       (== old-id (cd-arrow-id object))))
         (cd-focus-navigation body))
       (set! cd-interaction 'pending-arrow)
       (set! cd-interaction-id (cd-arrow-id object))
       (when (and (== old-kind 'arrow) (== old-id (cd-arrow-id object)))
         (set! cd-interaction-part 'edit)))
      (else
       (let* ((p (cd-snap-point (list x y)))
              (v (cd-add-vertex body (car p) (cadr p))))
         (cd-select 'vertex (cd-vertex-id v))
         (set! cd-interaction 'created)
         (cd-focus-object v 3))))
    (cd-refresh)
    "done"))

(define (cd-drag-distance x y)
  (if cd-press-point
      (cd-distance x y (car cd-press-point) (cadr cd-press-point))
      0.0))

(define (cd-update-loop-angle centre x y)
  (let ((dx (- x (car centre))) (dy (- y (cadr centre))))
    (when (> (+ (* dx dx) (* dy dy)) 0.01)
      (set! cd-drag-loop-angle
            (* 57.29577951308232 (atan dy dx))))))

(define (cd-update-drag-target body x y)
  (let* ((target (cd-nearest-vertex body x y cd-vertex-move-radius))
         (target-id (and target (cd-vertex-id target))))
    (set! cd-drag-target target-id)
    (cond
      ((== cd-interaction 'connecting)
       (and-with source (cd-find-vertex body cd-interaction-id)
         (cd-update-loop-angle
           (list (cd-vertex-x source) (cd-vertex-y source)) x y)))
      ((== cd-interaction 'reconnecting)
       (and-with arrow (cd-find-arrow body cd-interaction-id)
         (let ((fixed-id
                 (if (== cd-interaction-part 'source)
                     (cd-arrow-target arrow) (cd-arrow-source arrow))))
           (and-with fixed (cd-find-vertex body fixed-id)
             (cd-update-loop-angle
               (list (cd-vertex-x fixed) (cd-vertex-y fixed)) x y))))))))

(define (cd-handle-drag body x y)
  (cond
    ((== cd-interaction 'pending-connect)
     (when (> (cd-drag-distance x y) cd-drag-threshold)
       (set! cd-interaction 'connecting)))
    ((== cd-interaction 'pending-reconnect)
     (when (> (cd-drag-distance x y) cd-drag-threshold)
       (set! cd-interaction 'reconnecting))))
  (cond
    ((== cd-interaction 'moving)
     (and-with v (cd-find-vertex body cd-interaction-id)
       (let* ((p (cd-snap-point (list x y)))
              (occupied (cd-nearest-vertex body (car p) (cadr p) 0.20)))
         (when (or (not occupied) (== (cd-vertex-id occupied) (cd-vertex-id v)))
           (tree-set v 1 (number->string (car p)))
           (tree-set v 2 (number->string (cadr p)))))))
    ((or (== cd-interaction 'connecting)
         (== cd-interaction 'reconnecting))
     (set! cd-drag-point (list x y))
     (cd-update-drag-target body x y))
    (else #f))
  (cd-refresh)
  "done")

(define (cd-handle-release body x y)
  (let ((interaction cd-interaction) (part cd-interaction-part)
        (object-id cd-interaction-id) (target cd-drag-target))
    (cond
      ((and (== interaction 'connecting) target)
       (let ((a (cd-add-arrow body object-id target)))
         (when (== target object-id)
           (cd-set-option! a "loop-angle"
                           (number->string (or cd-drag-loop-angle 0.0))))
         (cd-select 'arrow (cd-arrow-id a))
         (cd-focus-object a 3)))
      ((and (== interaction 'reconnecting) target)
       (and-with a (cd-find-arrow body object-id)
         (let ((changed? #f))
           (cond ((and (== part 'source) (!= target (cd-arrow-source a)))
                  (tree-set a 1 target)
                  (set! changed? #t))
                 ((and (== part 'target) (!= target (cd-arrow-target a)))
                  (tree-set a 2 target)
                  (set! changed? #t)))
           (when (and changed?
                      (== (cd-arrow-source a) (cd-arrow-target a)))
             (cd-set-option! a "loop-angle"
                             (number->string (or cd-drag-loop-angle 0.0)))))))
      ((and (== interaction 'pending-connect) (== part 'edit))
       (and-with v (cd-find-vertex body object-id) (cd-focus-object v 3)))
      ((and (== interaction 'pending-arrow) (== part 'edit))
       (and-with a (cd-find-arrow body object-id) (cd-focus-object a 3)))
      (else #f))
    (cd-reset-interaction)
    (cd-refresh)
    "done"))

(define (cd-handle-hover body x y)
  (cd-begin-session body)
  (let* ((hit (cd-hit-test body x y)) (kind (cd-hit-kind hit))
         (object (cd-hit-object hit))
         (hover-kind
           (cond ((or (== kind 'handle) (== kind 'arrow)) 'arrow)
                 ((or (== kind 'vertex-content) (== kind 'vertex-move))
                  'vertex)
                 (else #f)))
         (hover-id
           (cond ((== hover-kind 'arrow) (cd-arrow-id object))
                 ((== hover-kind 'vertex) (cd-vertex-id object))
                 (else #f))))
    (when (cd-set-hover hover-kind hover-id) (cd-refresh))
    "done"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Keyboard navigation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (cd-cursor-body)
  (tree-innermost 'cd-body #t))

(define (cd-editing-label?)
  (or (tree-innermost 'cd-vertex #t)
      (tree-innermost 'cd-arrow #t)))

(tm-define (commutative-diagram-keyboard?)
  (and cd-session-body cd-selected-kind cd-selected-id
       (and-with body (cd-cursor-body)
         (and (== (tree->path body) cd-session-body)
              (not (cd-editing-label?))))))

(define (cd-object-position body kind object)
  (if (== kind 'vertex)
      (list (cd-vertex-x object) (cd-vertex-y object))
      (and-with geometry (cd-arrow-geometry body object)
        (cd-bezier-point geometry 0.5))))

(define (cd-navigation-objects body)
  (append
    (map (lambda (v)
           (list 'vertex (cd-vertex-id v)
                 (cd-object-position body 'vertex v)))
         (cd-vertices body))
    (list-filter
      (map (lambda (a)
             (and-with position (cd-object-position body 'arrow a)
               (list 'arrow (cd-arrow-id a) position)))
           (cd-arrows body))
      identity)))

(define (cd-selected-navigation-object body)
  (let ((object (if (== cd-selected-kind 'vertex)
                    (cd-find-vertex body cd-selected-id)
                    (cd-find-arrow body cd-selected-id))))
    (and object
         (list cd-selected-kind cd-selected-id
               (cd-object-position body cd-selected-kind object)))))

(define (cd-direction-vector direction)
  (case direction
    ((left) '(-1.0 0.0))
    ((right) '(1.0 0.0))
    ((up) '(0.0 1.0))
    ((down) '(0.0 -1.0))
    (else '(0.0 0.0))))

(define (cd-navigation-score origin candidate direction)
  (let* ((delta (list (- (car candidate) (car origin))
                      (- (cadr candidate) (cadr origin))))
         (primary (+ (* (car delta) (car direction))
                     (* (cadr delta) (cadr direction)))))
    (and (> primary 0.000001)
         (let* ((perpendicular
                  (abs (- (* (car delta) (cadr direction))
                          (* (cadr delta) (car direction)))))
                (distance (sqrt (+ (* (car delta) (car delta))
                                   (* (cadr delta) (cadr delta))))))
           (+ (* 4.0 (/ perpendicular primary))
              (* 0.05 distance))))))

(define (cd-navigate direction)
  (and cd-session-body
    (let* ((body (path->tree cd-session-body))
           (current (and (tree? body)
                         (cd-selected-navigation-object body)))
           (vector (cd-direction-vector direction))
           (best #f)
           (best-score 1.0e100))
      (when current
        (for (candidate (cd-navigation-objects body))
          (when (!= (list-ref candidate 1) cd-selected-id)
            (and-with score
              (cd-navigation-score (list-ref current 2)
                                   (list-ref candidate 2) vector)
              (when (< score best-score)
                (set! best candidate)
                (set! best-score score)))))
        (when best
          (cd-select (list-ref best 0) (list-ref best 1))
          (cd-set-hover #f #f)
          (cd-focus-navigation body)
          (cd-refresh))))))

(define (cd-edit-selected-label)
  (and cd-session-body
    (let ((body (path->tree cd-session-body)))
      (and (tree? body)
        (and-with object
          (if (== cd-selected-kind 'vertex)
              (cd-find-vertex body cd-selected-id)
              (cd-find-arrow body cd-selected-id))
          (cd-focus-object object 3))))))

(define (cd-delete-selected)
  (and cd-session-body
    (let ((body (path->tree cd-session-body))
          (kind cd-selected-kind)
          (id cd-selected-id))
      (when (tree? body)
        (let loop ((i (- (tree-arity body) 1)))
          (when (>= i 0)
            (let ((child (tree-ref body i)))
              (when (or (and (== kind 'arrow)
                             (tm-is? child 'cd-arrow)
                             (== (cd-arrow-id child) id))
                        (and (== kind 'vertex)
                             (or (and (tm-is? child 'cd-vertex)
                                      (== (cd-vertex-id child) id))
                                 (and (tm-is? child 'cd-arrow)
                                      (or (== (cd-arrow-source child) id)
                                          (== (cd-arrow-target child) id))))))
                (tree-remove! body i 1)))
            (loop (- i 1))))
        (cd-select #f #f)
        (cd-set-hover #f #f)
        (cd-focus-navigation body)
        (cd-refresh)))))

(define (cd-clear-selection)
  (cd-select #f #f)
  (cd-set-hover #f #f)
  (cd-refresh))

(kbd-map
  (:mode commutative-diagram-keyboard?)
  ("left" (cd-navigate 'left))
  ("right" (cd-navigate 'right))
  ("up" (cd-navigate 'up))
  ("down" (cd-navigate 'down))
  ("return" (cd-edit-selected-label))
  ("delete" (cd-delete-selected))
  ("backspace" (cd-delete-selected))
  ("escape" (cd-clear-selection)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Arrow properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (cd-selected-arrow-tree)
  (and cd-session-body (== cd-selected-kind 'arrow)
       (let ((body (path->tree cd-session-body)))
         (and (tree? body) (cd-find-arrow body cd-selected-id)))))

(define (cd-selected-option key fallback)
  (and-with a (cd-selected-arrow-tree) (cd-option a key fallback)))

(define (cd-negate-string s)
  (number->string (- (or (string->number s) 0.0))))

(define (cd-swap-side value top bottom)
  (cond ((== value top) bottom) ((== value bottom) top) (else value)))

(define (cd-current-options-with a key value)
  (let* ((old (tm-ref a 4))
         (opts (if (tm-is? old 'tuple) (tm-children old) '()))
         (result '()) (found? #f))
    (let loop ((xs opts))
      (when (pair? xs)
        (if (and (pair? (cdr xs)) (== (cd-string (car xs)) key))
            (begin
              (set! result (append result (list key value)))
              (set! found? #t)
              (loop (cddr xs)))
            (begin
              (set! result (append result
                (if (pair? (cdr xs)) (list (car xs) (cadr xs)) (list (car xs)))))
              (loop (if (pair? (cdr xs)) (cddr xs) '()))))))
    `(tuple ,@result ,@(if found? '() (list key value)))))

(define (cd-set-option! a key value)
  (tree-set a 4 (cd-current-options-with a key value)))

(define (cd-set-selected-option key value)
  (and-with a (cd-selected-arrow-tree)
    (cd-set-option! a key value)
    (cd-refresh)))

(define (cd-live-input-value answer)
  (if (and (pair? answer) (pair? (cdr answer))) (car answer) answer))

(define (cd-live-input-type name)
  (string-append name "#form-cd-arrow:string"))

(define (cd-reverse-selected-arrow)
  (and-with a (cd-selected-arrow-tree)
    (let ((source (cd-arrow-source a)) (target (cd-arrow-target a)))
      (tree-set a 1 target)
      (tree-set a 2 source)
      (cd-set-option! a "offset"
                      (cd-negate-string (cd-option a "offset" "0")))
      (cd-set-option! a "curve"
                      (cd-negate-string (cd-option a "curve" "0")))
      (cd-set-option! a "loop-radius"
                      (cd-negate-string (cd-option a "loop-radius" "3")))
      (cd-set-option! a "label-alignment"
        (cd-swap-side (cd-option a "label-alignment" "left")
                      "left" "right"))
      (cd-set-option! a "label-position"
        (number->string (- 100 (max 0 (min 100
          (cd-option-number a "label-position" 50))))))
      (cd-set-option! a "tail"
        (cd-swap-side (cd-option a "tail" "none")
                      "top-hook" "bottom-hook"))
      (cd-set-option! a "head"
        (cd-swap-side (cd-option a "head" "arrowhead")
                      "top-harpoon" "bottom-harpoon"))
      (cd-refresh))))

(define (cd-flip-selected-arrow)
  (and-with a (cd-selected-arrow-tree)
    (cd-set-option! a "offset"
                    (cd-negate-string (cd-option a "offset" "0")))
    (cd-set-option! a "curve"
                    (cd-negate-string (cd-option a "curve" "0")))
    (cd-set-option! a "loop-radius"
                    (cd-negate-string (cd-option a "loop-radius" "3")))
    (cd-set-option! a "label-alignment"
      (cd-swap-side (cd-option a "label-alignment" "left")
                    "left" "right"))
    (cd-set-option! a "tail"
      (cd-swap-side (cd-option a "tail" "none")
                    "top-hook" "bottom-hook"))
    (cd-set-option! a "head"
      (cd-swap-side (cd-option a "head" "arrowhead")
                    "top-harpoon" "bottom-harpoon"))
    (cd-refresh)))

(define (cd-flip-selected-label)
  (and-with a (cd-selected-arrow-tree)
    (cd-set-option! a "label-alignment"
      (cd-swap-side (cd-option a "label-alignment" "left")
                    "left" "right"))
    (cd-refresh)))

(tm-widget (cd-arrow-properties-widget cmd)
  (resize "48em" "38em"
    (padded
      (vertical
        (bold (text "Arrow style"))
        (horizontal
          (aligned
            (item (text "Edge type")
              (enum (cd-set-selected-option "edge-type" answer)
                '("arrow" "adjunction" "corner" "corner-inverse")
                (cd-selected-option "edge-type" "arrow") "15em"))
            (item (text "Tail")
              (enum (cd-set-selected-option "tail" answer)
                '("mono" "none" "maps-to" "top-hook" "bottom-hook"
                  "arrowhead")
                (cd-selected-option "tail" "none") "15em"))
            (item (text "Body")
              (enum (cd-set-selected-option "body" answer)
                '("solid" "none" "dashed" "dotted" "squiggly" "barred"
                  "double-barred" "bullet-solid" "bullet-hollow")
                (cd-selected-option "body" "solid") "15em"))
            (item (text "Head")
              (enum (cd-set-selected-option "head" answer)
                '("arrowhead" "none" "epi" "top-harpoon" "bottom-harpoon")
                (cd-selected-option "head" "arrowhead") "15em"))
            (item (text "Level (1-4)")
              (enum (cd-set-selected-option "level" answer)
                    '("1" "2" "3" "4")
                    (cd-selected-option "level" "1") "15em")))
          // //
          (aligned
            (item (text "Curve (-5..5)")
              (input (cd-set-selected-option
                       "curve" (cd-live-input-value answer))
                     (cd-live-input-type "curve")
                     (list (cd-selected-option "curve" "0")) "12em"))
            (item (text "Transverse edge offset")
              (input (cd-set-selected-option
                       "offset" (cd-live-input-value answer))
                     (cd-live-input-type "offset")
                     (list (cd-selected-option "offset" "0")) "12em"))
            (item (text "Shorten source (%)")
              (input (cd-set-selected-option
                       "shorten-source" (cd-live-input-value answer))
                     (cd-live-input-type "shorten-source")
                     (list (cd-selected-option "shorten-source" "0")) "12em"))
            (item (text "Shorten target (%)")
              (input (cd-set-selected-option
                       "shorten-target" (cd-live-input-value answer))
                     (cd-live-input-type "shorten-target")
                     (list (cd-selected-option "shorten-target" "0")) "12em"))
            (item (text "Loop radius (-5..5)")
              (input (cd-set-selected-option
                       "loop-radius" (cd-live-input-value answer))
                     (cd-live-input-type "loop-radius")
                     (list (cd-selected-option "loop-radius" "3")) "12em"))
            (item (text "Loop angle (-180..180)")
              (input (cd-set-selected-option
                       "loop-angle" (cd-live-input-value answer))
                     (cd-live-input-type "loop-angle")
                     (list (cd-selected-option "loop-angle" "0")) "12em"))))
        ===
        (bold (text "Label and colours"))
        (aligned
          (item (text "Label alignment")
            (enum (cd-set-selected-option "label-alignment" answer)
              '("left" "centre" "over" "right")
              (cd-selected-option "label-alignment" "left") "14em"))
          (item (text "Label position (0..100)")
            (input (cd-set-selected-option
                     "label-position" (cd-live-input-value answer))
                   (cd-live-input-type "label-position")
                   (list (cd-selected-option "label-position" "50")) "14em"))
          (item (text "Arrow colour")
            (input (cd-set-selected-option
                     "color" (cd-live-input-value answer))
                   (cd-live-input-type "color")
                   (list (cd-selected-option "color" "black")) "14em"))
          (item (text "Label colour")
            (input (cd-set-selected-option
                     "label-color" (cd-live-input-value answer))
                   (cd-live-input-type "label-color")
                   (list (cd-selected-option "label-color" "black")) "14em")))
        ===
        (horizontal
          ("Reverse" (cd-reverse-selected-arrow))
          // //
          ("Flip arrow" (cd-flip-selected-arrow))
          // //
          ("Flip label" (cd-flip-selected-label)))
        ===
        (bottom-buttons
          ("Close" (cmd)))))))

(define (cd-open-arrow-properties)
  (delayed (:idle 1)
    (ads-floating-tool-pane cd-arrow-properties-widget noop
                            "Commutative Diagram Arrow")))

(define (cd-current-diagram)
  (and cd-session-body
       (let* ((body (path->tree cd-session-body))
              (diagram (and (tree? body) (tree-up body))))
         (and (tree? diagram) (tm-is? diagram 'commutative-diagram)
              diagram))))

(define (cd-set-diagram-size! diagram width height)
  (tree-set diagram 0 (number->string (max 1.0 width)))
  (tree-set diagram 1 (number->string (max 1.0 height))))

(define (cd-enlarge horizontal?)
  (and-with diagram (cd-current-diagram)
    (let ((width (cd-number (tm-ref diagram 0) 8.1))
          (height (cd-number (tm-ref diagram 1) 3.1)))
      (cd-set-diagram-size! diagram
        (+ width (if horizontal? 1.0 0.0))
        (+ height (if horizontal? 0.0 1.0))))))

(define (cd-content-points body)
  (append
    (map (lambda (v) (list (cd-vertex-x v) (cd-vertex-y v)))
         (cd-vertices body))
    (apply append
      (map (lambda (a) (or (cd-arrow-samples body a) '()))
           (cd-arrows body)))))

(define (cd-trim)
  (and-with diagram (cd-current-diagram)
    (let* ((body (tm-ref diagram 2))
           (points (cd-content-points body)))
      (if (null? points)
          (cd-set-diagram-size! diagram 2.0 2.0)
          (let* ((xs (map car points)) (ys (map cadr points))
                 (xmin (apply min xs)) (xmax (apply max xs))
                 (ymin (apply min ys)) (ymax (apply max ys))
                 (cx (/ (+ xmin xmax) 2.0))
                 (cy (/ (+ ymin ymax) 2.0)))
            (for (v (cd-vertices body))
              (tree-set v 1 (number->string (- (cd-vertex-x v) cx)))
              (tree-set v 2 (number->string (- (cd-vertex-y v) cy))))
            (cd-set-diagram-size! diagram
              (+ (- xmax xmin) 1.2) (+ (- ymax ymin) 1.2)))))))

(tm-define (commutative-diagram-context-menu?)
  (and cd-context-kind cd-session-body
       (< (- (texmacs-time) cd-context-time) 1000)))

(tm-menu (commutative-diagram-popup-menu)
  (assuming (== cd-context-kind 'arrow)
    ("Arrow style" (cd-open-arrow-properties))
    ---)
  ("Trim" (cd-trim))
  ("Enlarge horizontally" (cd-enlarge #t))
  ("Enlarge vertically" (cd-enlarge #f)))

(tm-menu (commutative-diagram-focus-menu)
  (group "Commutative diagram")
  ((check "Show hidden" "v" #f) (commutative-diagram-show-hidden))
  ("Describe" (commutative-diagram-describe)))

(tm-menu (commutative-diagram-focus-icons)
  ((balloon (icon "tm_show_hidden.xpm")
            "Show commutative diagram structure")
   (commutative-diagram-show-hidden))
  ((balloon (icon "tm_focus_help.xpm")
            "Describe commutative diagram")
   (commutative-diagram-describe))
  //)

(define (cd-handle-adjust body x y)
  (cd-begin-session body)
  (let* ((hit (cd-hit-test body x y)) (kind (cd-hit-kind hit))
         (object (cd-hit-object hit)))
    (set! cd-context-time (texmacs-time))
    (if (or (== kind 'arrow) (== kind 'handle))
        (begin
          (cd-select 'arrow (cd-arrow-id object))
          (set! cd-context-kind 'arrow)
          (cd-refresh)
          "")
        (begin
          (set! cd-context-kind 'diagram)
          ""))))

(tm-define (commutative-diagram-handle TYPE X Y WIDTH HEIGHT BODY)
  (:secure #t)
  (if (not (tree? BODY)) ""
      (let ((type (cd-string TYPE)) (x (cd-coordinate X 0.0))
            (y (cd-coordinate Y 0.0)))
        (cond ((== type "click") (cd-handle-press BODY x y))
              ((== type "drag") (cd-handle-drag BODY x y))
              ((== type "select") (cd-handle-release BODY x y))
              ((or (== type "move") (== type "enter"))
               (cd-handle-hover BODY x y))
              ((== type "adjust") (cd-handle-adjust BODY x y))
              ((== type "leave")
               (let ((changed? (or cd-halos-visible?
                                   cd-hover-kind cd-hover-id)))
                 (set! cd-halos-visible? #f)
                 (cd-set-hover #f #f)
                 (when changed? (cd-refresh)))
               "done")
              ((== type "double-click")
               (let* ((hit (cd-hit-test BODY x y))
                      (object (cd-hit-object hit)))
                 (cond ((tm-is? object 'cd-vertex) (cd-focus-object object 3))
                       ((tm-is? object 'cd-arrow) (cd-focus-object object 3)))
                 (if object "done" "")))
              (else "")))))

(tm-define (make-cd)
  (:interactive #t)
  (insert-go-to '(commutative-diagram "8.1" "3.1" (cd-body ""))
                '(2 0)))
