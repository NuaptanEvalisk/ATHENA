;; Establish a real editor/view backed by the deliberately anonymous buffer
;; allocator.  Some tree-edit regressions exercise metadata mechanics without
;; activating the persistent source-node model; using the user-facing New
;; buffer would now inject born-v2 identity semantics into those tests.
(open-window)
(let ((anonymous (buffer-new)))
  (switch-to-buffer anonymous))
