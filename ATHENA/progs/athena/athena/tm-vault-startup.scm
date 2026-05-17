(texmacs-module (athena athena tm-vault-startup)
  (:use (kernel boot abbrevs)
        (athena athena tm-vault-recents)))

(define (vault-startup-unavailable-message dir-s)
  (show-message
   (string-append "Last vault is unavailable:\n" dir-s)
   "Vault unavailable"))

(define (vault-startup-available? dir)
  (url-exists? (url-append dir "Vaultfile")))

(define (vault-startup-open-welcome?)
  (== (get-preference "vault welcome page") "on"))

(define (vault-startup-show-explorer?)
  (== (get-preference "vault explorer show on startup") "on"))

(tm-define (vault-track-current-buffer-if-enabled)
  (if (and (== (get-preference "vault explorer track current file") "on")
           (vault-active?)
           (current-buffer))
      (vault-explorer-track-file (current-buffer))))

(tm-define (vault-show-explorer-and-track)
  (vault-show-explorer)
  (vault-track-current-buffer-if-enabled))

(define (vault-startup-show-explorer)
  (if (vault-startup-show-explorer?)
      (delayed (:idle 100)
        (if (vault-active?) (vault-show-explorer-and-track)))))

(tm-define (vault-startup-open-initial-buffer)
  (let* ((auto-load? (== (get-preference "vault auto load last") "on"))
         (report-missing? (== (get-preference "vault report missing last") "on"))
         (recent-vaults (get-recent-vaults))
         (latest-vault (if (pair? recent-vaults) (car recent-vaults) #f)))
    (cond
      ((not auto-load?)
       (if (vault-startup-open-welcome?) (go-to-welcome-page)))
      ((not latest-vault) #f)
      (else
       (let ((dir (string->url latest-vault)))
         (if (vault-startup-available? dir)
             (begin
               (load-vault-dir dir)
               (vault-startup-show-explorer)
               (if (vault-startup-open-welcome?) (go-to-welcome-page)))
             (if report-missing?
                 (vault-startup-unavailable-message latest-vault))))))))
