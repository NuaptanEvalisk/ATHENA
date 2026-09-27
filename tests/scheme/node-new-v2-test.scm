;; New ordinary source documents are born in XML-v2/identity mode.  This runs
;; against real BufferActors in an isolated profile; no existing file is
;; upgraded by the fixture.
(define (check condition label)
  (unless condition (error "New XML v2 source regression" label)))

(define root (getenv "ATHENA_NODE_NEW_V2_ROOT"))
(define name (string->url (string-append root "/created.ath")))
(define inserted-id #f)

(define (finish value)
  (call-with-output-file (string-append root "/result.scm")
    (lambda (port) (write value port)))
  (quit-TeXmacs))

(define (fail . args)
  (exec-global (lambda () (finish args))))

(define (run-created)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?)
             "new named source did not activate node identities")
      (check (not (equal? (tree-node-id (buffer-tree)) ""))
             "new source body has no UUID")
      (check (and (> (tree-arity (buffer-tree)) 0)
                  (not (equal? (tree-node-id (tree-ref (buffer-tree) 0)) "")))
             "new source initial paragraph has no UUID")
      (buffer-set-default-style)
      (start-editing)
      (tree-insert (buffer-tree) (tree-arity (buffer-tree))
                   (list (stree->tree "Created paragraph")))
      (commit-changes)
      (set! inserted-id
            (tree-node-id
              (tree-ref (buffer-tree) (- (tree-arity (buffer-tree)) 1))))
      (check (not (equal? inserted-id ""))
             "ordinary edit in new source did not receive UUID")
      (check (not (buffer-save name)) "first save of new source failed")

      ;; The user-facing New command uses a separate source-buffer allocator;
      ;; transient DataArt still uses make_new_buffer and is not tested here.
      (exec-global
        (lambda ()
          (let ((scratch (new-buffer)))
            (check (exec-buffer scratch
                     (lambda ()
                       (check (source-node-identities-active?)
                              "New scratch buffer is not identity-active")
                       (check (not (equal? (tree-node-id (buffer-tree)) ""))
                              "New scratch body has no UUID")))
                   "could not enter New scratch BufferActor")
            (finish (list #t inserted-id))))))
    fail))

(catch #t
  (lambda ()
    (check (not (buffer-create-source name)) "native v2 source creation failed")
    (switch-to-buffer name)
    (check (exec-buffer name run-created) "could not enter created source owner"))
  (lambda args (finish args)))
