(texmacs-module (test private-definition))

(define (private-definition value offset multiplier)
  (* (+ value offset) multiplier))

(tm-define (private-definition value)
  (private-definition value 2 3))

(tm-define (call-private-definition)
  (private-definition 4 1 2))
