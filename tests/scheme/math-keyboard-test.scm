;; Validate the shipped native keymap sources without depending on the removed
;; Scheme keyboard modules.
(use-modules (ice-9 textual-ports))

(define root (cadr (command-line)))
(define (slurp name)
  (call-with-input-file (string-append root "/ATHENA/misc/input/" name)
    get-string-all))
(define (check condition label)
  (unless condition (error label)))
(define (contains? text fragment)
  (not (not (string-contains text fragment))))

(define math (slurp "math-keybindings.json"))
(define generic (slurp "generic-keybindings.json"))
(define prefixes (slurp "keyboard-prefixes.json"))

(check (contains? prefixes "\"variant_key\": \"tab\"")
       "Tab is the variant key")
(check (contains? prefixes "\"unvariant_key\": \"S-tab\"")
       "Shift-Tab is the reverse variant key")

(for-each
  (lambda (key)
    (check (contains? math (string-append "\"key\": \"" key "\""))
           (string-append "missing native math binding " key)))
  '("- var" "- >" "- -" "math:right | var" "|" "| var"
    "| var var" "| var var var" "math:right |"))

(for-each
  (lambda (command)
    (check (contains? generic (string-append "\"call\": \"" command "\""))
           (string-append "missing native hybrid command " command)))
  '("hybrid-kbd-curly-left" "hybrid-kbd-curly-right"
    "hybrid-kbd-backslash" "hybrid-kbd-sub" "hybrid-kbd-sup"
    "hybrid-kbd-formula-open"))

(display "PASS: native math keymap, prefix and hybrid sources\n")
