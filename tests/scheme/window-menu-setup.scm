;; Runs on the GUI owner, with HOME isolated by evaluation-bar-test.py.
(do ((i 1 (+ i 1))) ((> i 2))
  (open-window)
  (let ((name (string->url
                (string-append (getenv "HOME") "/menu-"
                               (number->string i) ".ath"))))
    (buffer-rename (current-buffer) name)
    (buffer-set-body name (stree->tree '(document "Menu fixture")))
    (buffer-set-title name (string-append "Menu " (number->string i)))))
