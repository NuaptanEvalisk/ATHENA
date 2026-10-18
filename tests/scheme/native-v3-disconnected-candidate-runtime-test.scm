;; Run in an isolated full ATHENA instance after a temporary XML v3 offline
;; stage. This exercises the *real* normal Vault entry point, not the detached
;; migration reader. The candidate is deliberately unpublishable and must be
;; rejected before ordinary startup can canonicalize SQLite or edit source.
(define root (getenv "ATHENA_V3_CANDIDATE_ROOT"))
(unless (and root (string? root))
  (error "Disconnected XML v3 candidate fixture was not supplied"))
(define result
  (vault-load-with-ns (string->url root) "Disconnected migration fixture"
                      "map.sqlite" "ns.sqlite"))
(unless (and (string? result)
             (string-contains result "Unpublished XML v3 migration candidates cannot be opened"))
  (error "DISCONNECTED_V3_VAULT_WAS_NOT_REJECTED" result))
(display "PASS: disconnected XML v3 Vault cannot enter normal runtime\n")
