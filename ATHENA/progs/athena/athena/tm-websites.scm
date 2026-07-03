(texmacs-module (athena athena tm-websites)
  (:use (kernel boot abbrevs)
        (kernel athena tm-define)
        (kernel athena tm-secure)))

(define-secure-symbols websites-manager-show
                       open-websites-manager)

(tm-define (open-websites-manager)
  (:interactive #t)
  (websites-manager-show))
