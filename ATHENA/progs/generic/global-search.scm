;; Global search entry points and result navigation.
;; Copyright (C) 2026 Nuaptan. GPL version 3 or later.

(texmacs-module (generic global-search)
  (:use (generic generic-edit) (utils library cursor)))

(tm-define (global-search-open-result u p)
  (load-buffer u)
  (delayed (:idle 100)
    (lambda ()
      (go-to (append (tree->path (buffer-tree)) p)))))

(tm-define (global-search-open-occurrence u start end)
  (load-buffer u)
  (delayed (:idle 100)
    (lambda ()
      (let* ((root (tree->path (buffer-tree)))
             (p (append root start))
             (q (append root end)))
        (go-to p)
        (when (!= p q)
          (selection-set p q)
          (show-selection))))))

(tm-define (open-global-search)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first." "Global search")
      (global-search-show)))
