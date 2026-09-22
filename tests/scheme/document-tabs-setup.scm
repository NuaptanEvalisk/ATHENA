;; GUI-owner regression checks. All files live in the runner's temporary HOME.
(define (check ok message)
  (unless ok (error "Document tab regression" message)))
(define (fixture name)
  (let ((u (system->url (string-append (getenv "HOME") "/" name ".ath"))))
    (buffer-set-body u (stree->tree `(document ,name)))
    u))

;; Startup opens a disposable shell before loading the configured home page.
;; The home page must replace that shell, not leave an extra No name tab.
(define startup-buffer (current-buffer))
(define startup-window (current-window))
(define startup-count (length (window-list)))
(define home (fixture "home"))
(load-browse-buffer home)
(check (= (length (window-list)) startup-count) "home left a startup scratch tab")
(check (equal? (current-window) startup-window) "home did not reuse startup shell")
(check (not (buffer-exists? startup-buffer)) "startup scratch buffer was retained")

;; An explicitly opened blank document is not a startup placeholder.
(open-window)
(define (tab-count) (length (window-list)))
(define initial-count (tab-count))
(define a (current-buffer))
(define a-window (current-window))
(define a-view (current-view-url))
(define b (fixture "target"))
(define hidden (fixture "background"))
(check (= (tab-count) initial-count) "background buffers opened tabs")

;; The common link loader must open a new tab and retain the source tab.
(load-browse-buffer b)
(define b-window (current-window))
(define b-view (current-view-url))
(check (= (tab-count) (+ initial-count 1)) "link replaced source tab")
(check (equal? (window-to-buffer a-window) a) "source tab lost its buffer")
(check (equal? (current-buffer) b) "link did not select target")
(check (not (equal? a-window b-window)) "documents share a tab")

;; Same-document navigation and repeated cross-document navigation retain views.
(load-browse-buffer b)
(check (equal? (current-view-url) b-view) "same-document link replaced its view")
(switch-to-buffer a)
(check (equal? (current-view-url) a-view) "returning to source created a view")
(load-browse-buffer b)
(check (equal? (current-view-url) b-view) "returning to target created a view")
(check (= (tab-count) (+ initial-count 1)) "repeated link duplicated tab")
(check (= (length (buffer->views b)) 1) "repeated link created passive views")

;; Explicit opens also reuse an existing target instead of cloning its buffer.
(switch-to-buffer a)
(open-buffer-in-window b (buffer-get b) "")
(check (equal? (current-view-url) b-view) "explicit open cloned target")
(switch-to-buffer a)
(load-buffer-in-new-window b)
(check (equal? (current-view-url) b-view) "explicit load failed to focus target")
(window-set-buffer a-window b)
(check (equal? (window-to-buffer a-window) a) "legacy setter displaced source")

;; New document and Workspace New tab both allocate a distinct visible buffer.
(new-document)
(define scratch (current-buffer))
(define scratch-window (current-window))
(check (= (tab-count) (+ initial-count 2)) "new document did not get a tab")
(check (not (equal? scratch b)) "new document reused target buffer")
(buffer-close scratch)
(check (= (tab-count) (+ initial-count 1)) "closing buffer left its tab")
(check (not (buffer-exists? scratch)) "closed buffer remains registered")
(check (equal? (window-to-buffer a-window) a) "close replaced source tab")
(check (equal? (window-to-buffer b-window) b) "close replaced target tab")

;; Closing a noncurrent document must not change the focused document.
(switch-to-buffer a)
(buffer-close b)
(check (equal? (current-view-url) a-view) "background close changed focus")
(check (= (tab-count) initial-count) "background close left a tab")
(buffer-close hidden)
(check (= (tab-count) initial-count) "hidden buffer close changed tabs")
(check (equal? (current-view-url) a-view) "hidden close changed focus")

;; Direct switches retain their lazy-load behavior for unopened disk files.
(define disk (system->url (string-append (getenv "HOME") "/disk.ath")))
(tree-export
  (stree->tree '(document (TeXmacs "2.1.4") (style (tuple "generic"))
                         (body (document "Loaded from disk"))))
  disk "texmacs")
(switch-to-buffer disk)
(check (equal? (tree->stree (buffer-get-body disk))
               '(document "Loaded from disk")) "switch did not load file contents")
(check (equal? (window-to-buffer a-window) a) "disk load displaced source tab")
(buffer-close disk)

;; Leave another live tab for the actor-owned publication checks.
(new-document)
(buffer-set-body (current-buffer) (stree->tree '(document "Tab regression")))
