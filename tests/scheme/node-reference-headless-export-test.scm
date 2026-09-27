;; Run with -H -x (load ...), in an isolated ATHENA_HOME_PATH. The supplied
;; ATHENA_NODE_EXPORT_TEST_ROOT must be a new empty temporary directory.
(define root (getenv "ATHENA_NODE_EXPORT_TEST_ROOT"))
(unless (and root (> (string-length root) 0)) (error "Missing isolated test root"))
(define (check condition label)
  (unless condition (error "Headless node export regression" label)))
(define (path leaf) (string->url (string-append root "/" leaf)))
(define (write-file leaf text)
  (call-with-output-file (string-append root "/" leaf)
    (lambda (port) (display text port))))
(define id "11111111-1111-4111-8111-111111111111")
(define source (path "source.ath"))
(buffer-set source '(document (style "generic")
                     (body (document "Plain export without an active vault."))))
(switch-to-buffer source)
(check (not (buffer-export source (path "plain.pdf") "pdf")) "plain export")
(write-file "Vaultfile.json" "{\"name\":\"Isolated node export\"}")
(write-file "target.ath"
  (string-append
    "<athena-document version=\"2\" text-model=\"utf-8\">"
    "<node tag=\"document\"><node tag=\"style\"><text><value>generic</value></text></node>"
    "<node tag=\"body\"><node tag=\"document\"><text id=\"" id "\">"
    "<value>FROZEN REFERENCE PAYLOAD</value></text></node></node></node></athena-document>"))
(check (equal? (vault-load-with-ns (string->url root) "Export test" "map.sqlite" "ns.sqlite") "")
       "load isolated vault")
(buffer-set-body source (stree->tree `(document "Export with a native UUID target."
                                      (transclude (tuple ,id)))))
(check (not (buffer-export source (path "reference.pdf") "pdf")) "reference export")
;; An actor-owned target overrides its disk version. No Qt completion callback
;; may be necessary while the global coordinator waits for its source capture.
(buffer-set (path "target.ath") '(document (style "generic")
                                  (body (document "Live source deleted that identity."))))
(switch-to-buffer (path "target.ath"))
(check (not (buffer-export source (path "missing.pdf") "pdf")) "live deletion export")
(display "ATHENA-NODE-HEADLESS-EXPORT-PASS\n")
