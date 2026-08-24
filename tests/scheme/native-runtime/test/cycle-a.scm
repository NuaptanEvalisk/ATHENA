(texmacs-module (test cycle-a)
  (#:use (test cycle-b)))

(tm-define (cycle-a-value)
  'a)
