(texmacs-module (test legacy-compat)
  (#:use (test duplicate-source)))

(tm-define (native-forward-reference)
  native-forward-binding)

(tm-define (native-duplicate-reference)
  native-duplicate-binding)
