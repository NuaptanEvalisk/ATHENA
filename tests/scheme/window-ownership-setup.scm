;; Runs on the GUI owner, with HOME isolated by evaluation-bar-test.py.
(do ((i 1 (+ i 1))) ((> i 2))
  (open-window)
  (let ((name (string->url
                 (string-append (getenv "HOME") "/window-"
                                (number->string i) ".ath"))))
    (buffer-rename (current-buffer) name)
    (buffer-set-body name (stree->tree '(document "Window fixture")))
    (buffer-set-title name (string-append "Window " (number->string i)))))
