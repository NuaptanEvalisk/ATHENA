(texmacs-module (test dispatch-extension))

(tm-define (cross-module-dispatch value)
  (:require (> value 0))
  (list 'positive value))
