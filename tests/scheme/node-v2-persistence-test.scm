;; One vertical integration check for normal XML v2 persistence. The Python
;; driver supplies only isolated files/profile; all source mutation is owned by
;; real BufferActors.
(define (check condition label)
  (unless condition (error "Node v2 persistence regression" label)))

(define root (getenv "ATHENA_NODE_V2_ROOT"))
(define name (string->url (string-append root "/source.ath")))
(define autosave-name (string->url (string-append root "/source.ath~")))
(define reopen-name (string->url (string-append root "/reopen.ath")))
(define recovered-name (string->url (string-append root "/recovered.ath")))
(define expected-root-id "11111111-1111-4111-8111-111111111111")
(define expected-first-id "22222222-2222-4222-8222-222222222222")
(define saved-id #f)
(define autosave-id #f)

(define (finish value)
  (call-with-output-file (string-append root "/result.scm")
    (lambda (port) (write value port)))
  (quit-TeXmacs))

(define (fail args)
  (exec-global (lambda () (finish args))))

(define (run-recovered)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "recovery reactivates v2 identities")
      (check (= (tree-arity (buffer-tree)) 4) "autosave recovery keeps newer source")
      (check (equal? (tree-node-id (buffer-tree)) expected-root-id)
             "recovery root identity")
      (check (equal? (tree-node-id (tree-ref (buffer-tree) 2)) saved-id)
             "recovery keeps normally saved identity")
      (check (equal? (tree-node-id (tree-ref (buffer-tree) 3)) autosave-id)
             "recovery keeps autosaved identity")
      ;; The recovered target did not exist. Normal save must create it as v2,
      ;; using the source document's persistence mode rather than filename state.
      (check (not (buffer-save recovered-name)) "recovered v2 saves normally")
      (display "ATHENA-NODE-V2-PERSISTENCE-PASS\n")
      (exec-global (lambda () (finish #t))))
    fail))

(define (recover-autosave)
  (catch #t
    (lambda ()
      (check (not (buffer-import recovered-name autosave-name "texmacs"))
             "native autosave imports through v2-aware buffer path")
      (switch-to-buffer recovered-name)
      (check (exec-buffer recovered-name run-recovered)
             "enter recovered owner"))
    (lambda args (finish args))))

(define (run-reopened)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "reopen automatically activates identities")
      (check (= (tree-arity (buffer-tree)) 3) "normal save excludes later autosave edit")
      (check (equal? (tree-node-id (buffer-tree)) expected-root-id)
             "reopen root identity")
      (check (equal? (tree-node-id (tree-ref (buffer-tree) 0)) expected-first-id)
             "reopen original paragraph identity")
      (check (equal? (tree-node-id (tree-ref (buffer-tree) 2)) saved-id)
             "reopen generated identity")
      (exec-global recover-autosave))
    fail))

(define (reopen-saved)
  (catch #t
    (lambda ()
      (system-copy name reopen-name)
      (check (not (buffer-load reopen-name)) "normal v2 reopen")
      (switch-to-buffer reopen-name)
      (check (exec-buffer reopen-name run-reopened) "enter reopened owner"))
    (lambda args (finish args))))

(define (run-source)
  (catch #t
    (lambda ()
      (init-style "generic")
      (check (source-node-identities-active?) "normal v2 load activates identities")
      (check (equal? (tree-node-id (buffer-tree)) expected-root-id)
             "loaded root identity")
      (check (equal? (tree-node-id (tree-ref (buffer-tree) 0)) expected-first-id)
             "loaded paragraph identity")
      (check (equal? (cadr (assoc "test:marker"
                                  (tree-node-properties (tree-ref (buffer-tree) 0))))
                     '(string "kept"))
             "loaded typed property")

      (start-editing)
      (tree-insert (buffer-tree) 2 (list (stree->tree "Saved paragraph")))
      (commit-changes)
      (set! saved-id (tree-node-id (tree-ref (buffer-tree) 2)))
      (check (not (equal? saved-id "")) "ordinary edit allocates persistent UUID")
      (check (not (buffer-save name)) "normal v2 save")

      ;; This edit intentionally exists only in the autosave. The native v2
      ;; export path must finalize/preserve its UUID without downgrading through
      ;; the legacy TeXmacs serializer.
      (start-editing)
      (tree-insert (buffer-tree) 3 (list (stree->tree "Autosave paragraph")))
      (commit-changes)
      (set! autosave-id (tree-node-id (tree-ref (buffer-tree) 3)))
      (check (not (equal? autosave-id "")) "autosave edit receives UUID")
      (check (not (buffer-export name autosave-name "texmacs"))
             "native v2 autosave export")
      (exec-global reopen-saved))
    fail))

(catch #t
  (lambda ()
    (check (not (buffer-load name)) "normal v2 load")
    (switch-to-buffer name)
    (check (exec-buffer name run-source) "enter source owner"))
  (lambda args (finish args)))
