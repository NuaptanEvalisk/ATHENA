(texmacs-module (athena athena tm-vault-maintenance)
  (:use (kernel boot abbrevs)
        (kernel library list)))

(define (vault-maintenance-binary)
  (url-append (get-texmacs-path) "bin/ATHENA.bin"))

(define (vault-maintenance-save-open-buffers)
  (for-each
   (lambda (buf)
     (when (and (not (buffer-aux? buf)) (buffer-modified? buf))
       (save-buffer buf)))
   (buffer-list)))

(define (vault-maintenance-join-pass-ids ids)
  (if (null? ids) ""
      (let loop ((rest (cdr ids)) (out (car ids)))
        (if (null? rest) out
            (loop (cdr rest) (string-append out "," (car rest)))))))

(define (vault-maintenance-launch-command root skipped enabled)
  (let* ((bin (url->system (vault-maintenance-binary)))
         (root-s (url->system root))
         (bin-q (escape-shell bin))
         (root-q (escape-shell root-s))
         (skipped-q
          (escape-shell (vault-maintenance-join-pass-ids skipped)))
         (enabled-q
          (escape-shell (vault-maintenance-join-pass-ids enabled)))
         (take-prefs? (if (== (get-preference "vault take preferences with vault")
                              "on")
                          "on" "off")))
    (string-append "( sleep 1; cd " root-q " && "
                   "ATHENA_VAULT_MAINTENANCE_TAKE_PREFS=" take-prefs? " "
                   "ATHENA_VAULT_MAINTENANCE_SKIP_PASSES=" skipped-q " "
                   "ATHENA_VAULT_MAINTENANCE_ENABLE_PASSES=" enabled-q " "
                   bin-q " --vault-maintenance " root-q " ) &")))

(tm-define (vault-maintenance)
  (:interactive #t)
  (cond ((not (vault-active?))
         (set-message "No active vault to maintain" "Vault maintenance"))
        ((not (url-exists? (vault-maintenance-binary)))
         (set-message "Cannot find ATHENA binary for vault maintenance"
                      "Vault maintenance"))
        (else
         (with setup (vault-maintenance-setup (vault-get-root))
           (when (and (tree? setup) (tree-func? setup 'tuple)
                      (== (tree-arity setup) 3)
                      (== (tree->string (tree-ref setup 0)) "accepted"))
             (let ((skipped
                    (map tree->string
                         (tree-children (tree-ref setup 1))))
                   (enabled
                    (map tree->string
                         (tree-children (tree-ref setup 2)))))
               (vault-maintenance-save-open-buffers)
               (let ((unsaved (filter (lambda (buf)
                                        (and (not (buffer-aux? buf))
                                             (buffer-modified? buf)))
                                      (buffer-list))))
                 (if (nnull? unsaved)
                     (set-message "Some documents could not be saved; vault maintenance was not started"
                                  "Vault maintenance")
                     (begin
                       (system (vault-maintenance-launch-command
                                (vault-get-root) skipped enabled))
                       (quit-TeXmacs))))))))))
