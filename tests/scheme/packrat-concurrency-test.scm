;; Real native grammars, not a model of the cache. The driver isolates HOME.
(use-modules (ice-9 threads))
(define (check value message) (unless value (error message)))
(packrat-define "ownership-base" "Word" (string->tree "a"))
(packrat-property "ownership-base" "Word" "type" "symbol")
(packrat-inherit "ownership-derived" "ownership-base")

(define gate (make-mutex))
(define changed (make-condition-variable))
(define ready 0)
(define updated? #f)
(define (accepts text)
  (packrat-correct? "ownership-derived" "Word" (string->tree text)))
(define workers '())
;; Keep thread creation out of top-level definition expansion.
(set! workers
  (map
   (lambda (index)
     (call-with-new-thread
      (lambda ()
        (check (not (accepts "b")) "unmatched input accepted")
        (do ((i 0 (+ i 1))) ((= i 32))
          (check (accepts "a") "inherited grammar missing in new owner"))
        (with-mutex gate
          (set! ready (+ ready 1))
          (broadcast-condition-variable changed)
          (let wait ()
            (unless updated?
              (wait-condition-variable changed gate)
              (wait))))
        ;; Same cached input, but the grammar changed in another thread.
        (check (not (accepts "a")) "stale parse survived grammar revision")
        (do ((i 0 (+ i 1))) ((= i 32))
          (check (accepts "b") "updated grammar missing in existing owner"))
        #t)
      (lambda args
        (format (current-error-port) "Grammar worker ~a failed: ~s\n" index args)
        (exit 1)
        #f)))
   '(0 1 2 3)))
(with-mutex gate
  (let wait ()
    (unless (= ready 4)
      (wait-condition-variable changed gate)
      (wait))))
(packrat-define "ownership-base" "Word" (string->tree "b"))
(packrat-inherit "ownership-derived" "ownership-base")
(with-mutex gate
  (set! updated? #t)
  (broadcast-condition-variable changed))
(for-each (lambda (worker) (check (join-thread worker) "grammar worker failed"))
          workers)
(check (join-thread (call-with-new-thread
                    (lambda () (and (accepts "b") (not (accepts "a"))))))
       "new owner did not replay the final grammar")
(display "PASS: owner-local grammar replay, inheritance and parse invalidation\n")
(primitive-load (string-append (dirname (current-filename))
                               "/two-buffer-save-test.scm"))
