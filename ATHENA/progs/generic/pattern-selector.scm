
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : pattern-selector.scm
;; DESCRIPTION : native background selector bridge
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic pattern-selector))

(define-public athena-vertical-gradient-source "athena-gradient-vertical")

(define (selector-stree x)
  (cond ((tree? x) (tree->stree x))
        ((tm? x) (tm->stree x))
        (else x)))

(define (selector-pattern? x)
  (and (pair? x) (== (car x) 'pattern) (>= (length x) 4)))

(define (selector-effect x)
  (if (and (selector-pattern? x) (>= (length x) 5)) (list-ref x 4) #f))

(define (selector-effect-value eff kind)
  (cond ((or (not (pair? eff)) (null? (cdr eff))) #f)
        ((== (car eff) kind)
         (if (>= (length eff) 3) (list-ref eff 2) #f))
        (else (selector-effect-value (cadr eff) kind))))

(define (selector-gradient-values eff)
  (if (and (pair? eff) (== (car eff) 'eff-gradient) (>= (length eff) 4))
      (list (cadr eff) (caddr eff) (cadddr eff))
      '("0" "black" "white")))

(define (selector-initial mode old width)
  (let* ((fallback
          (cond ((== mode "gradient")
                 `(pattern ,athena-vertical-gradient-source "100%" "100%"
                           (eff-gradient "0" "black" "white")))
                ((== mode "picture")
                 '(pattern "$ATHENA_PATH/misc/patterns/neutral-pattern.png"
                           "100%" "100%"))
                (else
                 `(pattern "$ATHENA_PATH/misc/patterns/neutral-pattern.png"
                           ,width "100@"))))
         (p (selector-stree (or old fallback)))
         (p (if (selector-pattern? p) p fallback))
         (eff (selector-effect p)))
    (if (== mode "gradient")
        (let ((g (selector-gradient-values eff)))
          (list (cadr p) (caddr p) (cadddr p)
                (car g) (cadr g) (caddr g)))
        (list (cadr p) (caddr p) (cadddr p)
              (or (selector-effect-value eff 'eff-recolor) "")
              (or (selector-effect-value eff 'eff-skin) "")))))

(define (selector-image-effect recol skin)
  (let ((effect "0"))
    (when (!= recol "") (set! effect `(eff-recolor ,effect ,recol)))
    (when (!= skin "") (set! effect `(eff-skin ,effect ,skin)))
    (if (== effect "0") '() (list effect))))

(define (selector-result mode values)
  (and (>= (length values) 3)
       (let ((source (list-ref values 0))
             (width  (list-ref values 1))
             (height (list-ref values 2)))
         (cond ((== mode "gradient")
                (and (>= (length values) 6)
                     `(pattern ,athena-vertical-gradient-source ,width ,height
                               (eff-gradient ,(list-ref values 3)
                                             ,(list-ref values 4)
                                             ,(list-ref values 5)))))
               (else
                `(pattern ,source ,width ,height
                          ,@(selector-image-effect
                             (if (>= (length values) 4) (list-ref values 3) "")
                             (if (>= (length values) 5) (list-ref values 4) ""))))))))

(define (open-native-background-selector mode cmd initial)
  (with values (native-background-selector mode initial)
    (and-with result (selector-result mode values)
      (cmd (apply tm-pattern (cdr result))))))

(tm-define (open-pattern-selector cmd w)
  (:interactive #t)
  (open-native-background-selector
    "pattern" cmd (selector-initial "pattern" #f w)))

(tm-define (open-gradient-selector cmd . opt-old)
  (:interactive #t)
  (open-native-background-selector
    "gradient" cmd
    (selector-initial "gradient" (and (pair? opt-old) (car opt-old)) "100%")))

(tm-define (open-background-picture-selector cmd . opt-old)
  (:interactive #t)
  (open-native-background-selector
    "picture" cmd
    (selector-initial "picture" (and (pair? opt-old) (car opt-old)) "100%")))
