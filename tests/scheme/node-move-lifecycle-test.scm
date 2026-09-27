;; Real BufferActor cut/paste integration for one-use source move credentials.
;; Both vaults and all buffers are isolated temporary fixtures.
(import-from (generic generic-edit))

(define (check condition label)
  (unless condition (error "Node move lifecycle regression" label)))

(define root1 (getenv "ATHENA_NODE_MOVE_VAULT1"))
(define root2 (getenv "ATHENA_NODE_MOVE_VAULT2"))
(define a (string->url (string-append root1 "/A.ath")))
(define b (string->url (string-append root1 "/B.ath")))
(define c (string->url (string-append root2 "/C.ath")))
(define move-id "22222222-2222-4222-8222-222222222222")

(define (finish value)
  (call-with-output-file (string-append root1 "/result.scm")
    (lambda (port) (write value port)))
  (quit-TeXmacs))

(define (fail . args)
  (exec-global (lambda () (finish args))))

(define (edit command)
  (start-editing)
  (command)
  (commit-changes))

(define (paste-at-end)
  (tree-go-to (buffer-tree) :end)
  (edit (lambda () (clipboard-paste "primary"))))

(define (select-child index)
  (let* ((body (buffer-tree))
         (count (tree-arity body))
         (start (tree->path (tree-ref body index) :start))
         (end (if (< (+ index 1) count)
                  (tree->path (tree-ref body (+ index 1)) :start)
                  (tree->path body :end))))
    (selection-set start end)))

(define (id-count node id)
  (+ (if (equal? (tree-node-id node) id) 1 0)
     (if (tree-atomic? node) 0
         (apply + (map (lambda (child) (id-count child id))
                       (tree-children node))))))

(define (find-id node id)
  (cond ((equal? (tree-node-id node) id) node)
        ((tree-atomic? node) #f)
        (else
          (let loop ((children (tree-children node)))
            (and (pair? children)
                 (or (find-id (car children) id)
                     (loop (cdr children))))))))

(define (move-node-ids node)
  (append
    (if (equal? (tree->stree node) '(section "Move me"))
        (list (tree-node-id node)) '())
    (if (tree-atomic? node) '()
        (apply append (map move-node-ids (tree-children node))))))

(define (run-b-after-cross-vault)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "B remains an identified v2 source")
      (check (= (id-count (buffer-tree) move-id) 0)
             "cross-vault cut still removed source identity")
      ;; The C paste was the first paste attempt for that cut. It copied and
      ;; consumed the credential, so returning to the source vault cannot later
      ;; resurrect the old UUID as a move.
      (paste-at-end)
      (define repeated (move-node-ids (buffer-tree)))
      (check (= (length repeated) 1) "repeat paste returns content")
      (check (not (equal? (car repeated) move-id))
             "repeat after cross-vault paste remains a copy")
      ;; Undo the repeat copy, then the old cut. Its credential was discarded,
      ;; so the cut is now ordinary local history and restores the old source.
      (undo 0)
      (check (= (length (move-node-ids (buffer-tree))) 0) "undo repeat copy")
      (undo 0)
      (check (= (id-count (buffer-tree) move-id) 1)
             "ordinary undo restores cross-vault cut")
      (check (member move-id (move-node-ids (buffer-tree)))
             "ordinary cut undo restores original UUID")
      (display "ATHENA-NODE-MOVE-LIFECYCLE-PASS\n")
      (exec-global (lambda () (finish #t))))
    fail))

(define (return-to-first-vault)
  (catch #t
    (lambda ()
      (check (equal? (vault-load-with-ns
                       (string->url root1) "Move vault 1"
                       "map.sqlite" "ns.sqlite") "")
             "restore first isolated vault")
      (switch-to-buffer b)
      (check (exec-buffer b run-b-after-cross-vault)
             "enter B after cross-vault copy"))
    (lambda args (finish args))))

(define (run-c-cross-vault)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "C is an identified v2 source")
      (check (= (length (move-node-ids (buffer-tree))) 0) "C baseline")
      (paste-at-end)
      (define copied (move-node-ids (buffer-tree)))
      (check (= (length copied) 1) "cross-vault paste inserts copy")
      (check (not (equal? (car copied) move-id))
             "cross-vault paste renews UUID")
      (exec-global return-to-first-vault))
    fail))

(define (paste-into-second-vault)
  (catch #t
    (lambda ()
      (check (equal? (vault-load-with-ns
                       (string->url root2) "Move vault 2"
                       "map.sqlite" "ns.sqlite") "")
             "load second isolated vault")
      (check (not (buffer-load c)) "load C")
      (switch-to-buffer c)
      (check (exec-buffer c run-c-cross-vault) "enter C"))
    (lambda args (finish args))))

(define (run-b-after-redo)
  (catch #t
    (lambda ()
      (check (= (id-count (buffer-tree) move-id) 1)
             (list "redo restores move target"
                   (move-node-ids (buffer-tree))
                   (tree->stree (buffer-tree))))
      (check (member move-id (move-node-ids (buffer-tree)))
             "redo restores same moved UUID")
      ;; A new cut issues a new one-use credential. Its first paste is into a
      ;; different vault and therefore must use new-object semantics.
      (define moved (find-id (buffer-tree) move-id))
      (check (tree? moved) "find moved source before second cut")
      (tree-select moved)
      (edit (lambda () (clipboard-cut "primary")))
      (check (= (id-count (buffer-tree) move-id) 0)
             "second cut removes moved source")
      (exec-global paste-into-second-vault))
    fail))

(define (verify-b-redo)
  (switch-to-buffer b)
  (unless (exec-buffer b run-b-after-redo)
    (finish '(failed "enter B after coordinated redo"))))

(define (run-a-after-undo)
  (catch #t
    (lambda ()
      (check (= (id-count (buffer-tree) move-id) 1)
             "coordinated undo restores source")
      (check (member move-id (move-node-ids (buffer-tree)))
             "coordinated undo restores original UUID")
      ;; Invoke redo from the source side. It must delete A first, then reapply
      ;; the B insertion so the UUID never exists in both documents.
      (redo 0)
      (check (= (id-count (buffer-tree) move-id) 0)
             "coordinated redo deletes source")
      (exec-global verify-b-redo))
    fail))

(define (verify-a-undo)
  (switch-to-buffer a)
  (unless (exec-buffer a run-a-after-undo)
    (finish '(failed "enter A after coordinated undo"))))

(define (run-b-first-paste)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "B activates v2 identities")
      (check (= (length (move-node-ids (buffer-tree))) 0) "B baseline")
      (paste-at-end)
      (define first-paste (move-node-ids (buffer-tree)))
      (check (= (length first-paste) 1) "first paste inserts moved source")
      (check (equal? (car first-paste) move-id)
             "same-vault first paste preserves UUID")

      (paste-at-end)
      (define second-paste (move-node-ids (buffer-tree)))
      (check (= (length second-paste) 2) "second paste inserts another object")
      (check (= (length (filter (lambda (id) (equal? id move-id)) second-paste)) 1)
             "second paste keeps exactly one moved UUID")
      (check (= (length (filter (lambda (id) (not (equal? id move-id)))
                                second-paste)) 1)
             "second paste renews UUID")

      ;; Remove the ordinary duplicate. The next undo reaches the move marker
      ;; and must coordinate both actors.
      (undo 0)
      (check (equal? (move-node-ids (buffer-tree)) (list move-id))
             "undo second paste")
      (undo 0)
      (check (= (id-count (buffer-tree) move-id) 0)
             "coordinated undo removes target")
      (exec-global verify-a-undo))
    fail))

(define (paste-into-b)
  (switch-to-buffer b)
  (unless (exec-buffer b run-b-first-paste)
    (finish '(failed "enter B"))))

(define (run-a-cut)
  (catch #t
    (lambda ()
      (check (source-node-identities-active?) "A activates v2 identities")
      (check (= (id-count (buffer-tree) move-id) 1) "A baseline")
      (check (member move-id (move-node-ids (buffer-tree)))
             "fixture move UUID")
      (select-child 0)
      (edit (lambda () (clipboard-cut "primary")))
      (check (= (id-count (buffer-tree) move-id) 0) "cut removes source")
      (exec-global paste-into-b))
    fail))

(catch #t
  (lambda ()
    (check (equal? (vault-load-with-ns
                     (string->url root1) "Move vault 1"
                     "map.sqlite" "ns.sqlite") "")
           "load first isolated vault")
    (check (not (buffer-load a)) "load A")
    (check (not (buffer-load b)) "load B")
    (switch-to-buffer a)
    (check (exec-buffer a run-a-cut) "enter A"))
  (lambda args (finish args)))
