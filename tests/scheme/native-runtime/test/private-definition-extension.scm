(texmacs-module (test private-definition-extension))

(tm-define (private-definition value)
  (:require (< value 0))
  'negative)
