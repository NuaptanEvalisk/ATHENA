(texmacs-module (test cycle-b)
  (#:use (test cycle-a)))

(tm-define (cycle-b-value)
  'b)
