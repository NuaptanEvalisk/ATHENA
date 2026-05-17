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

(define (vault-maintenance-launch-command root)
  (let* ((bin (url->system (vault-maintenance-binary)))
         (root-s (url->system root))
         (bin-q (escape-shell bin))
         (root-q (escape-shell root-s)))
    (string-append "( sleep 1; cd " root-q " && "
                   bin-q " --vault-maintenance " root-q " ) &")))

(tm-define (vault-maintenance)
  (:interactive #t)
  (cond ((not (vault-active?))
         (set-message "No active vault to maintain" "Vault maintenance"))
        ((not (url-exists? (vault-maintenance-binary)))
         (set-message "Cannot find ATHENA binary for vault maintenance"
                      "Vault maintenance"))
        (else
         (user-confirm
          "Vault maintenance will save open documents, quit ATHENA, back up the vault, and normalize image names. Continue?"
          #f
          (lambda (answ)
            (when answ
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
                               (vault-get-root)))
                      (quit-TeXmacs))))))))))
