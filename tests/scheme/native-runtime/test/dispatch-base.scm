(texmacs-module (test dispatch-base))

(tm-define (cross-module-dispatch value)
  (list 'base value))

(tm-define (call-cross-module-dispatch value)
  (cross-module-dispatch value))
