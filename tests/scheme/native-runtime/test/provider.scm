(texmacs-module (test provider)
  (:use (test duplicate-source)))

(set! provider-load-count (+ provider-load-count 1))

(tm-define (native-lazy-target value)
  (+ value 37))

(tm-define (native-lazy-conditioned value)
  (+ value 100))

(tm-define (native-dispatch value)
  'base)

(tm-define (native-dispatch value)
  (#:require (> value 0))
  'positive)

(tm-property (native-dispatch value)
  (#:synopsis "native dispatch"))
