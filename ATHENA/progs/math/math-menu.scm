
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : math-menu.scm
;; DESCRIPTION : menus for mathematical mode and mathematical symbols
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (math math-menu)
  (:use (math math-edit)
        (table table-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Inserting mathematical markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (open-latex-formula-dialog)
  (:interactive #t)
  (with vals (native-latex-formula-dialog)
    (when (>= (length vals) 2)
      (let* ((mode (first vals))
             (input (second vals)))
        (cond ((== mode "inline")
               (let* ((wrapped (string-append "$" input "$"))
                      (t (latex->texmacs (parse-latex wrapped)))
                      (c (if (and (tree-is? t 'document) (> (tree-arity t) 0))
                             (tree-ref t 0)
                             t))
                      (body (if (and (tree-is? c 'with)
                                     (>= (tree-arity c) 2)
                                     (== (tree-ref c 0) "mode")
                                     (== (tree-ref c 1) "math"))
                                (tree-ref c (1- (tree-arity c)))
                                c)))
                 (insert `(math ,body))))
              ((== mode "display")
               (let ((t (latex->texmacs
                          (parse-latex (string-append "\\[" input "\\]")))))
                 (when (tree? t)
                   (for (c (tree-children t)) (insert c)))))
              (else
               (let ((t (latex->texmacs (parse-latex input))))
                 (when (tree? t)
                   (for (c (tree-children t)) (insert c))))))))))

(menu-bind insert-math-menu
  ("Inline formula" (make 'math))
  (if (style-has? "env-math-dtd")
      ("Displayed formula" (make-equation*))
      ("Several equations" (make-eqnarray*)))
  ("Commutative diagram" (make-cd))
  ---
  ("LaTeX formula" (open-latex-formula-dialog)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; General purpose markup that is also relevant for mathematics
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-content-tag-menu
  ("Deleted" (make 'deleted))
  ("Fill out" (make 'fill-out))
  ("Marked" (make 'marked)))

(menu-bind math-presentation-tag-menu
  ("Decorated" (make 'decorated))
  (if (and (style-has? "std-markup-dtd")
           (== (get-preference "experimental alpha") "on"))
      ---
      ("Pastel" (make 'pastel))
      ("Greyed" (make 'greyed))
      ("Light" (make 'light))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special mathematical text properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-special-format-menu
  (-> "Display style"
      ("On" (make-with "math-display" "true"))
      ("Off" (make-with "math-display" "false")))
  (-> "Index level"
      ("Normal" (make-with "math-level" "0"))
      ("Script size" (make-with "math-level" "1"))
      ("Script script size" (make-with "math-level" "2")))
  (-> "Condensed"
      ("On" (make-with "math-condensed" "true"))
      ("Off" (make-with "math-condensed" "false"))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main Format menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-format-menu
  ("Font" (open-font-selector))
  (when (inside? 'table)
      ("Cell" (open-cell-properties))
      ("Table" (open-table-properties)))
  ---
  (link math-special-format-menu)
  ---
  (-> "Whitespace" (link horizontal-space-menu))
  (-> "Line break" (link line-break-menu))
  ---
  (-> "Color"
      (if (== (get-preference "experimental alpha") "on")
	  (-> "Opacity" (link opacity-menu))
	  ---)
      (link color-menu))
  (-> "Adjust" (link adjust-menu))
  (-> "Transform" (link linear-transform-menu))
  (-> "Specific" (link specific-menu))
  (-> "Special" (link format-special-menu))
  (-> "Font effects" (link text-font-effects-menu))
  (assuming (== (get-preference "bitmap effects") "on")
    (-> "Graphical effects" (link text-effects-menu))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The mathematical symbol menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind symbol-menu
  (-> "Large opening bracket" (tile 8 (link left-delimiter-menu)))
  (-> "Large separator" (tile 8 (link middle-delimiter-menu)))
  (-> "Large closing bracket" (tile 8 (link right-delimiter-menu)))
  (-> "Big operator"
      (tile 6 (link big-operator-menu)))
  ---
  (-> "Binary operator"
      (tile 8 (link binary-operation-menu)))
  (-> "Binary relation"
      (tile 8 (link binary-relation-menu-1))
      ---
      (tile 8 (link binary-relation-menu-2)))
  (-> "Arrow"
      (tile 9 (link horizontal-arrow-menu))
      ---
      (tile 8 (link vertical-arrow-menu))
      ---
      (tile 6 (link long-arrow-menu))
      ---
      (link extensible-arrow-menu))
  (-> "Negation"
      ("General negation" (key-press "/"))
      ---
      (tile 9 (link negation-menu-1))
      ---
      (tile 9 (link negation-menu-2)))
  ---
  (-> "Greek letter"
      (tile 8 (link lower-greek-menu))
      ---
      (tile 8 (link upper-greek-menu)))
  (-> "Miscellaneous"
      (tile 8 (link miscellaneous-symbol-menu))
      ---
      (tile 6 (link dots-menu))))

(menu-bind textual-operator-menu
  ("Normal" (make 'math-up))
  ("Italic" (make 'math-it))
  ("Bold" (make 'math-bf))
  ("Typewriter" (make 'math-tt))
  ("Sans serif" (make 'math-ss))
  ("Slanted" (make 'math-sl)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Large delimiters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind large-delimiter-menu
  (symbol "(" (math-bracket-open "(" ")" 'default))
  (symbol ")" (math-bracket-open ")" "(" 'default))
  (symbol "[" (math-bracket-open "[" "]" 'default))
  (symbol "]" (math-bracket-open "]" "[" 'default))
  (symbol "{" (math-bracket-open "{" "}" 'default))
  (symbol "}" (math-bracket-open "}" "{" 'default))
  (symbol "⟨" (math-bracket-open "⟨" "⟩" 'default))
  (symbol "⟩" (math-bracket-open "⟩" "⟨" 'default))
  (symbol "⌊" (math-bracket-open "⌊" "⌋" 'default))
  (symbol "⌋" (math-bracket-open "⌋" "⌊" 'default))
  (symbol "⌈" (math-bracket-open "⌈" "⌉" 'default))
  (symbol "⌉" (math-bracket-open "⌉" "⌈" 'default))
  (symbol "⟦"
          (math-bracket-open "⟦" "⟧" 'default))
  (symbol "⟧"
          (math-bracket-open "⟧" "⟦" 'default))
  (symbol "|" (math-bracket-open "|" "|" 'default))
  (symbol "‖" (math-bracket-open "‖" "‖" 'default))
  (symbol "/" (math-bracket-open "/" "\\" 'default))
  (symbol "\\" (math-bracket-open "\\" "/" 'default))
  (symbol "."
          (math-bracket-open "." "." 'default)))

(menu-bind left-delimiter-menu
  (symbol "(" (math-bracket-open "(" ")" #t))
  (symbol ")" (math-bracket-open ")" "(" #t))
  (symbol "[" (math-bracket-open "[" "]" #t))
  (symbol "]" (math-bracket-open "]" "[" #t))
  (symbol "{" (math-bracket-open "{" "}" #t))
  (symbol "}" (math-bracket-open "}" "{" #t))
  (symbol "⟨" (math-bracket-open "⟨" "⟩" #t))
  (symbol "⟩" (math-bracket-open "⟩" "⟨" #t))
  (symbol "⌊" (math-bracket-open "⌊" "⌋" #t))
  (symbol "⌋" (math-bracket-open "⌋" "⌊" #t))
  (symbol "⌈" (math-bracket-open "⌈" "⌉" #t))
  (symbol "⌉" (math-bracket-open "⌉" "⌈" #t))
  (symbol "⟦"
          (math-bracket-open "⟦" "⟧" #t))
  (symbol "⟧"
          (math-bracket-open "⟧" "⟦" #t))
  (symbol "|" (math-bracket-open "|" "|" #t))
  (symbol "‖" (math-bracket-open "‖" "‖" #t))
  (symbol "/" (math-bracket-open "/" "\\" #t))
  (symbol "\\" (math-bracket-open "\\" "/" #t))
  (symbol "." (math-bracket-open "." "." #t)))

(menu-bind middle-delimiter-menu
  (symbol "(" (math-separator "(" #t))
  (symbol ")" (math-separator ")" #t))
  (symbol "[" (math-separator "[" #t))
  (symbol "]" (math-separator "]" #t))
  (symbol "{" (math-separator "{" #t))
  (symbol "}" (math-separator "}" #t))
  (symbol "⟨" (math-separator "⟨" #t))
  (symbol "⟩" (math-separator "⟩" #t))
  (symbol "⌊" (math-separator "⌊" #t))
  (symbol "⌋" (math-separator "⌋" #t))
  (symbol "⌈" (math-separator "⌈" #t))
  (symbol "⌉" (math-separator "⌉" #t))
  (symbol "⟦" (math-separator "⟦" #t))
  (symbol "⟧" (math-separator "⟧" #t))
  (symbol "|" (math-separator "|" #t))
  (symbol "‖" (math-separator "‖" #t))
  (symbol "/" (math-separator "/" #t))
  (symbol "\\" (math-separator "\\" #t)))

(menu-bind right-delimiter-menu
  (symbol "(" (math-bracket-close "(" ")" #t))
  (symbol ")" (math-bracket-close ")" "(" #t))
  (symbol "[" (math-bracket-close "[" "]" #t))
  (symbol "]" (math-bracket-close "]" "[" #t))
  (symbol "{" (math-bracket-close "{" "}" #t))
  (symbol "}" (math-bracket-close "}" "{" #t))
  (symbol "⟨" (math-bracket-close "⟨" "⟩" #t))
  (symbol "⟩" (math-bracket-close "⟩" "⟨" #t))
  (symbol "⌊" (math-bracket-close "⌊" "⌋" #t))
  (symbol "⌋" (math-bracket-close "⌋" "⌊" #t))
  (symbol "⌈" (math-bracket-close "⌈" "⌉" #t))
  (symbol "⌉" (math-bracket-close "⌉" "⌈" #t))
  (symbol "⟦"
          (math-bracket-close "⟦" "⟧" #t))
  (symbol "⟧"
          (math-bracket-close "⟧" "⟦" #t))
  (symbol "|" (math-bracket-close "|" "|" #t))
  (symbol "‖" (math-bracket-close "‖" "‖" #t))
  (symbol "/" (math-bracket-close "/" "\\" #t))
  (symbol "\\" (math-bracket-close "\\" "/" #t))
  (symbol "." (math-bracket-close "." "." #t)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Big operators
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind big-operator-menu
  (symbol "∫" (math-big-operator "∫"))
  (symbol "⨍" (math-big-operator "⨍"))
  (symbol "∬" (math-big-operator "∬"))
  (symbol "∭" (math-big-operator "∭"))
  (symbol "∫⋯∫" (math-big-operator '(named-symbol "texmacs:idotsint")))
  (symbol "∮" (math-big-operator "∮"))
  (symbol "∯" (math-big-operator "∯"))
  (symbol "∑" (math-big-operator "∑"))
  (symbol "∏" (math-big-operator "∏"))
  (symbol "∐" (math-big-operator "∐"))
  (symbol "⨀" (math-big-operator "⨀"))
  (symbol "⨁" (math-big-operator "⨁"))
  (symbol "⨂" (math-big-operator "⨂"))
  (symbol "⋂" (math-big-operator "⋂"))
  (symbol "⋃" (math-big-operator "⋃"))
  (symbol "⨄" (math-big-operator "⨄"))
  (symbol "⨅" (math-big-operator "⨅"))
  (symbol "⨆" (math-big-operator "⨆"))
  (symbol "□" (math-big-operator "□"))
  (symbol "⋀" (math-big-operator "⋀"))
  (symbol "⋁" (math-big-operator "⋁"))
  (symbol "⋏" (math-big-operator "⋏"))
  (symbol "⋎" (math-big-operator "⋎"))
  (symbol "▵" (math-big-operator "▵"))
  (symbol "▿" (math-big-operator "▿"))
  (symbol "∥" (math-big-operator "∥"))
  (symbol "⫴" (math-big-operator "⫴")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Binary operations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind binary-operation-menu
  (symbol "oplus" (insert '(named-symbol "texmacs:oplus")))
  (symbol "ominus" (insert '(named-symbol "texmacs:ominus")))
  (symbol "otimes" (insert '(named-symbol "texmacs:otimes")))
  (symbol "oslash" (insert '(named-symbol "texmacs:oslash")))
  (symbol "odot" (insert '(named-symbol "texmacs:odot")))
  (symbol "⊚")
  (symbol "circledast" (insert '(named-symbol "texmacs:circledast")))
  (symbol "obar" (insert '(named-symbol "texmacs:obar")))
  (symbol "⊞")
  (symbol "⊟")
  (symbol "⊠")
  (symbol "⧄")
  (symbol "⊡")
  (symbol "⧈")
  (symbol "⧆")
  (symbol "boxbar" (insert '(named-symbol "texmacs:boxbar")))

  (symbol "±")
  (symbol "∓")
  (symbol "×")
  (symbol "÷")
  (symbol "∗")
  (symbol "⋆")
  (symbol "∘")
  (symbol "•")
  (symbol "⋅")
  (symbol "∩")
  (symbol "∪")
  (symbol "⊎")
  (symbol "⊓")
  (symbol "⊔")
  (symbol "∨")
  (symbol "∧")
  
  (symbol "⋉")
  (symbol "⋊")
  (symbol "⋋")
  (symbol "⋌")
  (symbol "⋎")
  (symbol "⋏")
  (symbol "⊻")
  (symbol "⌅"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Binary relations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind binary-relation-menu-1
  (symbol "∼")
  (symbol "≃")
  (symbol "≈")
  (symbol "≅")
  (symbol "≍")
  (symbol "≡")
  (symbol "asympasymp" (insert '(named-symbol "texmacs:asympasymp")))
  (symbol "simsim" (insert '(named-symbol "texmacs:simsim")))
  (symbol "≏")
  (symbol "≎")
  (symbol "≗")
  (symbol "∽")
  (symbol "⋍")
  (symbol "≖")
  (symbol "thicksim" (insert '(named-symbol "texmacs:thicksim")))
  (symbol "thickapprox" (insert '(named-symbol "texmacs:thickapprox")))
  (symbol "≊")
  (symbol "≜")
  (symbol "≠")
  (symbol "∉")
  (symbol "⊥" (insert '(named-symbol "texmacs:perp")))
  (symbol "⌣")
  (symbol "⌢")
  (symbol "∝"))

(menu-bind binary-relation-menu-2
  (symbol "<")
  (symbol "⩽")
  (symbol "≤")
  (symbol "≦")
  (symbol "≪")
  (symbol "lleq" (insert '(named-symbol "texmacs:lleq")))
  (symbol "⋘")
  (symbol "llleq" (insert '(named-symbol "texmacs:llleq")))
  (symbol ">")
  (symbol "⩾")
  (symbol "≥")
  (symbol "≧")
  (symbol "≫")
  (symbol "ggeq" (insert '(named-symbol "texmacs:ggeq")))
  (symbol "⋙")
  (symbol "gggeq" (insert '(named-symbol "texmacs:gggeq")))

  (symbol "≺")
  (symbol "≼")
  (symbol "⪯")
  (symbol "≾")
  (symbol "precprec" (insert '(named-symbol "texmacs:precprec")))
  (symbol "precpreceq" (insert '(named-symbol "texmacs:precpreceq")))
  (symbol "precprecprec" (insert '(named-symbol "texmacs:precprecprec")))
  (symbol "precprecpreceq" (insert '(named-symbol "texmacs:precprecpreceq")))
  (symbol "≻")
  (symbol "≽")
  (symbol "⪰")
  (symbol "≿")
  (symbol "succsucc" (insert '(named-symbol "texmacs:succsucc")))
  (symbol "succsucceq" (insert '(named-symbol "texmacs:succsucceq")))
  (symbol "succsuccsucc" (insert '(named-symbol "texmacs:succsuccsucc")))
  (symbol "succsuccsucceq" (insert '(named-symbol "texmacs:succsuccsucceq")))

  (symbol "⊂")
  (symbol "⊆")
  (symbol "⫅")
  (symbol "⊏")
  (symbol "⊑")
  (symbol "⋐")
  (symbol "subsetplus" (insert '(named-symbol "texmacs:subsetplus")))
  (symbol "∈")
  (symbol "⊃")
  (symbol "⊇")
  (symbol "⫆")
  (symbol "⊐")
  (symbol "⊒")
  (symbol "⋑")
  (symbol "supsetplus" (insert '(named-symbol "texmacs:supsetplus")))
  (symbol "∋")

  (symbol "⊲")
  (symbol "trianglelefteqslant" (insert '(named-symbol "texmacs:trianglelefteqslant")))
  (symbol "⊴")
  (symbol "◂")
  (symbol "≲")
  (symbol "⪅")
  (symbol "≾")
  (symbol "⪷")
  (symbol "⊳")
  (symbol "trianglerighteqslant" (insert '(named-symbol "texmacs:trianglerighteqslant")))
  (symbol "⊵")
  (symbol "▸")
  (symbol "≳")
  (symbol "⪆")
  (symbol "≿")
  (symbol "⪸"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Arrows
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind horizontal-arrow-menu
  (symbol "←")
  (symbol "⇐")
  (symbol "↼")
  (symbol "↽")
  (symbol "⇇")
  (symbol "↢")
  (symbol "↩")
  (symbol "↫")
  (symbol "↞")

  (symbol "→")
  (symbol "⇒")
  (symbol "⇀")
  (symbol "⇁")
  (symbol "⇉")
  (symbol "↣")
  (symbol "↪")
  (symbol "↬")
  (symbol "↠")

  (symbol "↔")
  (symbol "⇔")
  (symbol "⇋")
  (symbol "⇌")
  (symbol "⇆")
  (symbol "⇄")
  (symbol "↦")
  (symbol "↝")
  (symbol "↭")

  (symbol "⇇")
  (symbol "threeleftarrows" (insert '(named-symbol "texmacs:threeleftarrows")))
  (symbol "fourleftarrows" (insert '(named-symbol "texmacs:fourleftarrows")))
  (symbol "⇉")
  (symbol "threerightarrows" (insert '(named-symbol "texmacs:threerightarrows")))
  (symbol "fourrightarrows" (insert '(named-symbol "texmacs:fourrightarrows")))
  (symbol "⇚")
  (symbol "⇛")
  (symbol "LRleftrightarrow" (insert '(named-symbol "texmacs:LRleftrightarrow"))))

(menu-bind vertical-arrow-menu
  (symbol "↑")
  (symbol "⇑")
  (symbol "⇈")
  (symbol "↿")
  (symbol "↾")
  (symbol "↖")
  (symbol "↗")
  (symbol "↕")

  (symbol "↓")
  (symbol "⇓")
  (symbol "⇊")
  (symbol "⇃")
  (symbol "⇂")
  (symbol "↙")
  (symbol "↘")
  (symbol "⇕"))

(menu-bind long-arrow-menu
  (symbol "⟵")
  (symbol "⟶")
  (symbol "⟷")
  (symbol "⟸")
  (symbol "⟹")
  (symbol "⟺")
  (symbol "longhookleftarrow" (insert '(named-symbol "texmacs:longhookleftarrow")))
  (symbol "longhookrightarrow" (insert '(named-symbol "texmacs:longhookrightarrow")))
  (symbol "⟼"))

(menu-bind extensible-arrow-menu
  ("Extensible left arrow" (make-long-arrow "←"))
  ("Extensible right arrow" (make-long-arrow "→")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Negations
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind negation-menu-1
  (symbol "≠")
  (symbol "≢")
  (symbol "≭")
  (symbol "≁")
  (symbol "≉")
  (symbol "≄")
  (symbol "≇")
  (symbol "∉")
  (symbol "∌"))

(menu-bind negation-menu-2
  (symbol "≮")
  (symbol "nleqslant" (insert '(named-symbol "texmacs:nleqslant")))
  (symbol "≰")
  (symbol "⪇")
  (symbol "≨")
  (symbol "lvertneqq" (insert '(named-symbol "texmacs:lvertneqq")))
  (symbol "⋦")
  (symbol "⪉")
  (symbol "⪵")

  (symbol "≯")
  (symbol "ngeqslant" (insert '(named-symbol "texmacs:ngeqslant")))
  (symbol "≱")
  (symbol "⪈")
  (symbol "≩")
  (symbol "gvertneqq" (insert '(named-symbol "texmacs:gvertneqq")))
  (symbol "⋧")
  (symbol "⪊")
  (symbol "⪶")

  (symbol "⊀")
  (symbol "⋠")
  (symbol "npreceq" (insert '(named-symbol "texmacs:npreceq")))
  (symbol "⋨")
  (symbol "⪹")
  (symbol "⊊")
  (symbol "⫋")
  (symbol "varsubsetneq" (insert '(named-symbol "texmacs:varsubsetneq")))
  (symbol "varsubsetneqq" (insert '(named-symbol "texmacs:varsubsetneqq")))

  (symbol "⊁")
  (symbol "⋡")
  (symbol "nsucceq" (insert '(named-symbol "texmacs:nsucceq")))
  (symbol "⋩")
  (symbol "⪺")
  (symbol "⊋")
  (symbol "⫌")
  (symbol "varsupsetneq" (insert '(named-symbol "texmacs:varsupsetneq")))
  (symbol "varsupsetneqq" (insert '(named-symbol "texmacs:varsupsetneqq")))

  (symbol "nsqsubset" (insert '(named-symbol "texmacs:nsqsubset")))
  (symbol "⋢")
  (symbol "nsqsupset" (insert '(named-symbol "texmacs:nsqsupset")))
  (symbol "⋣"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Greek characters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind lower-greek-menu
  (symbol "α")
  (symbol "β")
  (symbol "γ")
  (symbol "δ")
  (symbol "ε")
  (symbol "ϵ")
  (symbol "ζ")
  (symbol "η")
  (symbol "θ")
  (symbol "ϑ")
  (symbol "ι")
  (symbol "κ")
  (symbol "λ")
  (symbol "μ")
  (symbol "ν")
  (symbol "ξ")
  (symbol "ο")
  (symbol "π")
  (symbol "ϖ")
  (symbol "ρ")
  (symbol "ϱ")
  (symbol "σ")
  (symbol "ς")
  (symbol "τ")
  (symbol "υ")
  (symbol "ϕ")
  (symbol "φ")
  (symbol "χ")
  (symbol "ψ")
  (symbol "ω"))

(menu-bind upper-greek-menu
  (symbol "Γ")
  (symbol "Δ")
  (symbol "Θ")
  (symbol "Λ")
  (symbol "Ξ")
  (symbol "Π")
  (symbol "Σ")
  (symbol "Υ")
  (symbol "Φ")
  (symbol "Ψ")
  (symbol "Ω"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Miscellaneous symbols
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind miscellaneous-symbol-menu
  (symbol "d" (insert '(named-symbol "texmacs:mathd")))
  (symbol "i" (insert '(named-symbol "texmacs:mathi")))
  (symbol "e" (insert '(named-symbol "texmacs:mathe")))
  (symbol "γ" (insert '(named-symbol "texmacs:matheuler")))
  (symbol "π" (insert '(named-symbol "texmacs:mathpi")))
  (symbol "ı")
  (symbol "jmath" (insert '(named-symbol "texmacs:jmath")))
  (symbol "ℓ")

  (symbol "ℵ")
  (symbol "ℶ")
  (symbol "ℷ")
  (symbol "ℸ")
  (symbol "ℜ" (insert '(named-symbol "texmacs:Re")))
  (symbol "ℑ" (insert '(named-symbol "texmacs:Im")))
  (symbol "℧")
  (symbol "℘")

  (symbol "∅")
  (symbol "∅" (insert '(named-symbol "texmacs:varnothing")))
  (symbol "∞")
  (symbol "∂")
  (symbol "∇")
  (symbol "∀")
  (symbol "∃")
  (symbol "¬")

  (symbol "⊤")
  (symbol "⊥")
  (symbol "⊢")
  (symbol "⊩")
  (symbol "⊪")
  (symbol "⊨")
  (symbol "⊣")
  (symbol "∠")

  (symbol "box" (insert '(named-symbol "texmacs:box")))
  (symbol "⋄")
  (symbol "▵")
  (symbol "♣")
  (symbol "♦")
  (symbol "♥")
  (symbol "♠")
  (symbol "\\")

  (symbol "♭")
  (symbol "♮")
  (symbol "♯")
  (symbol "♪")
  (symbol "♩")
  (symbol "𝅗𝅥")
  (symbol "𝅝")
  (symbol "♫")

  (symbol "☼")
  (symbol "☾")
  (symbol "☽")
  (symbol "♁")
  (symbol "♂")
  (symbol "♀")
  (symbol "✠")
  (symbol "✠" (insert '(named-symbol "texmacs:kreuz")))

  (symbol "⌕")
  (symbol "☎")
  (symbol "✓" (insert '(named-symbol "texmacs:checked")))
  (symbol "☞" (insert '(named-symbol "texmacs:pointer")))
  (symbol "🔔" (insert '(named-symbol "texmacs:bell"))))

(menu-bind dots-menu
  (symbol "…")
  (symbol "⋯")
  (symbol "hdots" (insert '(named-symbol "texmacs:hdots")))
  (symbol "⋮")
  (symbol "⋱")
  (symbol "⋰"))

(menu-bind bold-num-menu
  (symbol "𝟎" (math-insert-alphabet 'math-alpha-bold "0"))
  (symbol "𝟏" (math-insert-alphabet 'math-alpha-bold "1"))
  (symbol "𝟐" (math-insert-alphabet 'math-alpha-bold "2"))
  (symbol "𝟑" (math-insert-alphabet 'math-alpha-bold "3"))
  (symbol "𝟒" (math-insert-alphabet 'math-alpha-bold "4"))
  (symbol "𝟓" (math-insert-alphabet 'math-alpha-bold "5"))
  (symbol "𝟔" (math-insert-alphabet 'math-alpha-bold "6"))
  (symbol "𝟕" (math-insert-alphabet 'math-alpha-bold "7"))
  (symbol "𝟖" (math-insert-alphabet 'math-alpha-bold "8"))
  (symbol "𝟗" (math-insert-alphabet 'math-alpha-bold "9")))

(menu-bind bold-alpha-menu
  (symbol "𝒂" (math-insert-alphabet 'math-alpha-bold "a"))
  (symbol "𝒃" (math-insert-alphabet 'math-alpha-bold "b"))
  (symbol "𝒄" (math-insert-alphabet 'math-alpha-bold "c"))
  (symbol "𝒅" (math-insert-alphabet 'math-alpha-bold "d"))
  (symbol "𝒆" (math-insert-alphabet 'math-alpha-bold "e"))
  (symbol "𝒇" (math-insert-alphabet 'math-alpha-bold "f"))
  (symbol "𝒈" (math-insert-alphabet 'math-alpha-bold "g"))
  (symbol "𝒉" (math-insert-alphabet 'math-alpha-bold "h"))
  (symbol "𝒊" (math-insert-alphabet 'math-alpha-bold "i"))
  (symbol "𝒋" (math-insert-alphabet 'math-alpha-bold "j"))
  (symbol "𝒌" (math-insert-alphabet 'math-alpha-bold "k"))
  (symbol "𝒍" (math-insert-alphabet 'math-alpha-bold "l"))
  (symbol "𝒎" (math-insert-alphabet 'math-alpha-bold "m"))
  (symbol "𝒏" (math-insert-alphabet 'math-alpha-bold "n"))
  (symbol "𝒐" (math-insert-alphabet 'math-alpha-bold "o"))
  (symbol "𝒑" (math-insert-alphabet 'math-alpha-bold "p"))
  (symbol "𝒒" (math-insert-alphabet 'math-alpha-bold "q"))
  (symbol "𝒓" (math-insert-alphabet 'math-alpha-bold "r"))
  (symbol "𝒔" (math-insert-alphabet 'math-alpha-bold "s"))
  (symbol "𝒕" (math-insert-alphabet 'math-alpha-bold "t"))
  (symbol "𝒖" (math-insert-alphabet 'math-alpha-bold "u"))
  (symbol "𝒗" (math-insert-alphabet 'math-alpha-bold "v"))
  (symbol "𝒘" (math-insert-alphabet 'math-alpha-bold "w"))
  (symbol "𝒙" (math-insert-alphabet 'math-alpha-bold "x"))
  (symbol "𝒚" (math-insert-alphabet 'math-alpha-bold "y"))
  (symbol "𝒛" (math-insert-alphabet 'math-alpha-bold "z"))
  (symbol "𝑨" (math-insert-alphabet 'math-alpha-bold "A"))
  (symbol "𝑩" (math-insert-alphabet 'math-alpha-bold "B"))
  (symbol "𝑪" (math-insert-alphabet 'math-alpha-bold "C"))
  (symbol "𝑫" (math-insert-alphabet 'math-alpha-bold "D"))
  (symbol "𝑬" (math-insert-alphabet 'math-alpha-bold "E"))
  (symbol "𝑭" (math-insert-alphabet 'math-alpha-bold "F"))
  (symbol "𝑮" (math-insert-alphabet 'math-alpha-bold "G"))
  (symbol "𝑯" (math-insert-alphabet 'math-alpha-bold "H"))
  (symbol "𝑰" (math-insert-alphabet 'math-alpha-bold "I"))
  (symbol "𝑱" (math-insert-alphabet 'math-alpha-bold "J"))
  (symbol "𝑲" (math-insert-alphabet 'math-alpha-bold "K"))
  (symbol "𝑳" (math-insert-alphabet 'math-alpha-bold "L"))
  (symbol "𝑴" (math-insert-alphabet 'math-alpha-bold "M"))
  (symbol "𝑵" (math-insert-alphabet 'math-alpha-bold "N"))
  (symbol "𝑶" (math-insert-alphabet 'math-alpha-bold "O"))
  (symbol "𝑷" (math-insert-alphabet 'math-alpha-bold "P"))
  (symbol "𝑸" (math-insert-alphabet 'math-alpha-bold "Q"))
  (symbol "𝑹" (math-insert-alphabet 'math-alpha-bold "R"))
  (symbol "𝑺" (math-insert-alphabet 'math-alpha-bold "S"))
  (symbol "𝑻" (math-insert-alphabet 'math-alpha-bold "T"))
  (symbol "𝑼" (math-insert-alphabet 'math-alpha-bold "U"))
  (symbol "𝑽" (math-insert-alphabet 'math-alpha-bold "V"))
  (symbol "𝑾" (math-insert-alphabet 'math-alpha-bold "W"))
  (symbol "𝑿" (math-insert-alphabet 'math-alpha-bold "X"))
  (symbol "𝒀" (math-insert-alphabet 'math-alpha-bold "Y"))
  (symbol "𝒁" (math-insert-alphabet 'math-alpha-bold "Z")))

(menu-bind bold-up-alpha-menu
  (symbol "𝐚" (math-insert-alphabet 'math-alpha-bold-up "a"))
  (symbol "𝐛" (math-insert-alphabet 'math-alpha-bold-up "b"))
  (symbol "𝐜" (math-insert-alphabet 'math-alpha-bold-up "c"))
  (symbol "𝐝" (math-insert-alphabet 'math-alpha-bold-up "d"))
  (symbol "𝐞" (math-insert-alphabet 'math-alpha-bold-up "e"))
  (symbol "𝐟" (math-insert-alphabet 'math-alpha-bold-up "f"))
  (symbol "𝐠" (math-insert-alphabet 'math-alpha-bold-up "g"))
  (symbol "𝐡" (math-insert-alphabet 'math-alpha-bold-up "h"))
  (symbol "𝐢" (math-insert-alphabet 'math-alpha-bold-up "i"))
  (symbol "𝐣" (math-insert-alphabet 'math-alpha-bold-up "j"))
  (symbol "𝐤" (math-insert-alphabet 'math-alpha-bold-up "k"))
  (symbol "𝐥" (math-insert-alphabet 'math-alpha-bold-up "l"))
  (symbol "𝐦" (math-insert-alphabet 'math-alpha-bold-up "m"))
  (symbol "𝐧" (math-insert-alphabet 'math-alpha-bold-up "n"))
  (symbol "𝐨" (math-insert-alphabet 'math-alpha-bold-up "o"))
  (symbol "𝐩" (math-insert-alphabet 'math-alpha-bold-up "p"))
  (symbol "𝐪" (math-insert-alphabet 'math-alpha-bold-up "q"))
  (symbol "𝐫" (math-insert-alphabet 'math-alpha-bold-up "r"))
  (symbol "𝐬" (math-insert-alphabet 'math-alpha-bold-up "s"))
  (symbol "𝐭" (math-insert-alphabet 'math-alpha-bold-up "t"))
  (symbol "𝐮" (math-insert-alphabet 'math-alpha-bold-up "u"))
  (symbol "𝐯" (math-insert-alphabet 'math-alpha-bold-up "v"))
  (symbol "𝐰" (math-insert-alphabet 'math-alpha-bold-up "w"))
  (symbol "𝐱" (math-insert-alphabet 'math-alpha-bold-up "x"))
  (symbol "𝐲" (math-insert-alphabet 'math-alpha-bold-up "y"))
  (symbol "𝐳" (math-insert-alphabet 'math-alpha-bold-up "z"))
  (symbol "𝐀" (math-insert-alphabet 'math-alpha-bold-up "A"))
  (symbol "𝐁" (math-insert-alphabet 'math-alpha-bold-up "B"))
  (symbol "𝐂" (math-insert-alphabet 'math-alpha-bold-up "C"))
  (symbol "𝐃" (math-insert-alphabet 'math-alpha-bold-up "D"))
  (symbol "𝐄" (math-insert-alphabet 'math-alpha-bold-up "E"))
  (symbol "𝐅" (math-insert-alphabet 'math-alpha-bold-up "F"))
  (symbol "𝐆" (math-insert-alphabet 'math-alpha-bold-up "G"))
  (symbol "𝐇" (math-insert-alphabet 'math-alpha-bold-up "H"))
  (symbol "𝐈" (math-insert-alphabet 'math-alpha-bold-up "I"))
  (symbol "𝐉" (math-insert-alphabet 'math-alpha-bold-up "J"))
  (symbol "𝐊" (math-insert-alphabet 'math-alpha-bold-up "K"))
  (symbol "𝐋" (math-insert-alphabet 'math-alpha-bold-up "L"))
  (symbol "𝐌" (math-insert-alphabet 'math-alpha-bold-up "M"))
  (symbol "𝐍" (math-insert-alphabet 'math-alpha-bold-up "N"))
  (symbol "𝐎" (math-insert-alphabet 'math-alpha-bold-up "O"))
  (symbol "𝐏" (math-insert-alphabet 'math-alpha-bold-up "P"))
  (symbol "𝐐" (math-insert-alphabet 'math-alpha-bold-up "Q"))
  (symbol "𝐑" (math-insert-alphabet 'math-alpha-bold-up "R"))
  (symbol "𝐒" (math-insert-alphabet 'math-alpha-bold-up "S"))
  (symbol "𝐓" (math-insert-alphabet 'math-alpha-bold-up "T"))
  (symbol "𝐔" (math-insert-alphabet 'math-alpha-bold-up "U"))
  (symbol "𝐕" (math-insert-alphabet 'math-alpha-bold-up "V"))
  (symbol "𝐖" (math-insert-alphabet 'math-alpha-bold-up "W"))
  (symbol "𝐗" (math-insert-alphabet 'math-alpha-bold-up "X"))
  (symbol "𝐘" (math-insert-alphabet 'math-alpha-bold-up "Y"))
  (symbol "𝐙" (math-insert-alphabet 'math-alpha-bold-up "Z")))

(menu-bind bold-greek-menu
  (symbol "𝜶" (math-insert-alphabet 'math-alpha-bold "α"))
  (symbol "𝜷" (math-insert-alphabet 'math-alpha-bold "β"))
  (symbol "𝜸" (math-insert-alphabet 'math-alpha-bold "γ"))
  (symbol "𝜹" (math-insert-alphabet 'math-alpha-bold "δ"))
  (symbol "𝝐" (math-insert-alphabet 'math-alpha-bold "ε"))
  (symbol "𝜺" (math-insert-alphabet 'math-alpha-bold "ε"))
  (symbol "𝜻" (math-insert-alphabet 'math-alpha-bold "ζ"))
  (symbol "𝜼" (math-insert-alphabet 'math-alpha-bold "η"))
  (symbol "𝜽" (math-insert-alphabet 'math-alpha-bold "θ"))
  (symbol "𝝑" (math-insert-alphabet 'math-alpha-bold "θ"))
  (symbol "𝜾" (math-insert-alphabet 'math-alpha-bold "ι"))
  (symbol "𝜿" (math-insert-alphabet 'math-alpha-bold "κ"))
  (symbol "𝝀" (math-insert-alphabet 'math-alpha-bold "λ"))
  (symbol "𝝁" (math-insert-alphabet 'math-alpha-bold "μ"))
  (symbol "𝝂" (math-insert-alphabet 'math-alpha-bold "ν"))
  (symbol "𝝃" (math-insert-alphabet 'math-alpha-bold "ξ"))
  (symbol "𝝄" (math-insert-alphabet 'math-alpha-bold "ο"))
  (symbol "𝝅" (math-insert-alphabet 'math-alpha-bold "π"))
  (symbol "𝝕" (math-insert-alphabet 'math-alpha-bold "π"))
  (symbol "𝝆" (math-insert-alphabet 'math-alpha-bold "ρ"))
  (symbol "𝝔" (math-insert-alphabet 'math-alpha-bold "ρ"))
  (symbol "𝝈" (math-insert-alphabet 'math-alpha-bold "σ"))
  (symbol "𝝇" (math-insert-alphabet 'math-alpha-bold "ς"))
  (symbol "𝝉" (math-insert-alphabet 'math-alpha-bold "τ"))
  (symbol "𝝊" (math-insert-alphabet 'math-alpha-bold "υ"))
  (symbol "𝝓" (math-insert-alphabet 'math-alpha-bold "φ"))
  (symbol "𝝋" (math-insert-alphabet 'math-alpha-bold "φ"))
  (symbol "𝝌" (math-insert-alphabet 'math-alpha-bold "χ"))
  (symbol "𝝍" (math-insert-alphabet 'math-alpha-bold "ψ"))
  (symbol "𝝎" (math-insert-alphabet 'math-alpha-bold "ω"))
  (symbol "𝚪" (math-insert-alphabet 'math-alpha-bold "Γ"))
  (symbol "𝚫" (math-insert-alphabet 'math-alpha-bold "Δ"))
  (symbol "𝚯" (math-insert-alphabet 'math-alpha-bold "Θ"))
  (symbol "𝚲" (math-insert-alphabet 'math-alpha-bold "Λ"))
  (symbol "𝚵" (math-insert-alphabet 'math-alpha-bold "Ξ"))
  (symbol "𝚷" (math-insert-alphabet 'math-alpha-bold "Π"))
  (symbol "𝚺" (math-insert-alphabet 'math-alpha-bold "Σ"))
  (symbol "𝚼" (math-insert-alphabet 'math-alpha-bold "Υ"))
  (symbol "𝚽" (math-insert-alphabet 'math-alpha-bold "Φ"))
  (symbol "𝚿" (math-insert-alphabet 'math-alpha-bold "Ψ"))
  (symbol "𝛀" (math-insert-alphabet 'math-alpha-bold "Ω")))

(menu-bind cal-menu
  (symbol "𝒜" (math-insert-alphabet 'math-alpha-cal "A"))
  (symbol "ℬ" (math-insert-alphabet 'math-alpha-cal "B"))
  (symbol "𝒞" (math-insert-alphabet 'math-alpha-cal "C"))
  (symbol "𝒟" (math-insert-alphabet 'math-alpha-cal "D"))
  (symbol "ℰ" (math-insert-alphabet 'math-alpha-cal "E"))
  (symbol "ℱ" (math-insert-alphabet 'math-alpha-cal "F"))
  (symbol "𝒢" (math-insert-alphabet 'math-alpha-cal "G"))
  (symbol "ℋ" (math-insert-alphabet 'math-alpha-cal "H"))
  (symbol "ℐ" (math-insert-alphabet 'math-alpha-cal "I"))
  (symbol "𝒥" (math-insert-alphabet 'math-alpha-cal "J"))
  (symbol "𝒦" (math-insert-alphabet 'math-alpha-cal "K"))
  (symbol "ℒ" (math-insert-alphabet 'math-alpha-cal "L"))
  (symbol "ℳ" (math-insert-alphabet 'math-alpha-cal "M"))
  (symbol "𝒩" (math-insert-alphabet 'math-alpha-cal "N"))
  (symbol "𝒪" (math-insert-alphabet 'math-alpha-cal "O"))
  (symbol "𝒫" (math-insert-alphabet 'math-alpha-cal "P"))
  (symbol "𝒬" (math-insert-alphabet 'math-alpha-cal "Q"))
  (symbol "ℛ" (math-insert-alphabet 'math-alpha-cal "R"))
  (symbol "𝒮" (math-insert-alphabet 'math-alpha-cal "S"))
  (symbol "𝒯" (math-insert-alphabet 'math-alpha-cal "T"))
  (symbol "𝒰" (math-insert-alphabet 'math-alpha-cal "U"))
  (symbol "𝒱" (math-insert-alphabet 'math-alpha-cal "V"))
  (symbol "𝒲" (math-insert-alphabet 'math-alpha-cal "W"))
  (symbol "𝒳" (math-insert-alphabet 'math-alpha-cal "X"))
  (symbol "𝒴" (math-insert-alphabet 'math-alpha-cal "Y"))
  (symbol "𝒵" (math-insert-alphabet 'math-alpha-cal "Z")))

(menu-bind frak-menu
  (symbol "𝔞" (math-insert-alphabet 'math-alpha-frak "a"))
  (symbol "𝔟" (math-insert-alphabet 'math-alpha-frak "b"))
  (symbol "𝔠" (math-insert-alphabet 'math-alpha-frak "c"))
  (symbol "𝔡" (math-insert-alphabet 'math-alpha-frak "d"))
  (symbol "𝔢" (math-insert-alphabet 'math-alpha-frak "e"))
  (symbol "𝔣" (math-insert-alphabet 'math-alpha-frak "f"))
  (symbol "𝔤" (math-insert-alphabet 'math-alpha-frak "g"))
  (symbol "𝔥" (math-insert-alphabet 'math-alpha-frak "h"))
  (symbol "𝔦" (math-insert-alphabet 'math-alpha-frak "i"))
  (symbol "𝔧" (math-insert-alphabet 'math-alpha-frak "j"))
  (symbol "𝔨" (math-insert-alphabet 'math-alpha-frak "k"))
  (symbol "𝔩" (math-insert-alphabet 'math-alpha-frak "l"))
  (symbol "𝔪" (math-insert-alphabet 'math-alpha-frak "m"))
  (symbol "𝔫" (math-insert-alphabet 'math-alpha-frak "n"))
  (symbol "𝔬" (math-insert-alphabet 'math-alpha-frak "o"))
  (symbol "𝔭" (math-insert-alphabet 'math-alpha-frak "p"))
  (symbol "𝔮" (math-insert-alphabet 'math-alpha-frak "q"))
  (symbol "𝔯" (math-insert-alphabet 'math-alpha-frak "r"))
  (symbol "𝔰" (math-insert-alphabet 'math-alpha-frak "s"))
  (symbol "𝔱" (math-insert-alphabet 'math-alpha-frak "t"))
  (symbol "𝔲" (math-insert-alphabet 'math-alpha-frak "u"))
  (symbol "𝔳" (math-insert-alphabet 'math-alpha-frak "v"))
  (symbol "𝔴" (math-insert-alphabet 'math-alpha-frak "w"))
  (symbol "𝔵" (math-insert-alphabet 'math-alpha-frak "x"))
  (symbol "𝔶" (math-insert-alphabet 'math-alpha-frak "y"))
  (symbol "𝔷" (math-insert-alphabet 'math-alpha-frak "z"))
  (symbol "𝔄" (math-insert-alphabet 'math-alpha-frak "A"))
  (symbol "𝔅" (math-insert-alphabet 'math-alpha-frak "B"))
  (symbol "ℭ" (math-insert-alphabet 'math-alpha-frak "C"))
  (symbol "𝔇" (math-insert-alphabet 'math-alpha-frak "D"))
  (symbol "𝔈" (math-insert-alphabet 'math-alpha-frak "E"))
  (symbol "𝔉" (math-insert-alphabet 'math-alpha-frak "F"))
  (symbol "𝔊" (math-insert-alphabet 'math-alpha-frak "G"))
  (symbol "ℌ" (math-insert-alphabet 'math-alpha-frak "H"))
  (symbol "ℑ" (math-insert-alphabet 'math-alpha-frak "I"))
  (symbol "𝔍" (math-insert-alphabet 'math-alpha-frak "J"))
  (symbol "𝔎" (math-insert-alphabet 'math-alpha-frak "K"))
  (symbol "𝔏" (math-insert-alphabet 'math-alpha-frak "L"))
  (symbol "𝔐" (math-insert-alphabet 'math-alpha-frak "M"))
  (symbol "𝔑" (math-insert-alphabet 'math-alpha-frak "N"))
  (symbol "𝔒" (math-insert-alphabet 'math-alpha-frak "O"))
  (symbol "𝔓" (math-insert-alphabet 'math-alpha-frak "P"))
  (symbol "𝔔" (math-insert-alphabet 'math-alpha-frak "Q"))
  (symbol "ℜ" (math-insert-alphabet 'math-alpha-frak "R"))
  (symbol "𝔖" (math-insert-alphabet 'math-alpha-frak "S"))
  (symbol "𝔗" (math-insert-alphabet 'math-alpha-frak "T"))
  (symbol "𝔘" (math-insert-alphabet 'math-alpha-frak "U"))
  (symbol "𝔙" (math-insert-alphabet 'math-alpha-frak "V"))
  (symbol "𝔚" (math-insert-alphabet 'math-alpha-frak "W"))
  (symbol "𝔛" (math-insert-alphabet 'math-alpha-frak "X"))
  (symbol "𝔜" (math-insert-alphabet 'math-alpha-frak "Y"))
  (symbol "ℨ" (math-insert-alphabet 'math-alpha-frak "Z")))

(menu-bind bbb-menu
  (symbol "𝕒" (math-insert-alphabet 'math-alpha-bbb "a"))
  (symbol "𝕓" (math-insert-alphabet 'math-alpha-bbb "b"))
  (symbol "𝕔" (math-insert-alphabet 'math-alpha-bbb "c"))
  (symbol "𝕕" (math-insert-alphabet 'math-alpha-bbb "d"))
  (symbol "𝕖" (math-insert-alphabet 'math-alpha-bbb "e"))
  (symbol "𝕗" (math-insert-alphabet 'math-alpha-bbb "f"))
  (symbol "𝕘" (math-insert-alphabet 'math-alpha-bbb "g"))
  (symbol "𝕙" (math-insert-alphabet 'math-alpha-bbb "h"))
  (symbol "𝕚" (math-insert-alphabet 'math-alpha-bbb "i"))
  (symbol "𝕛" (math-insert-alphabet 'math-alpha-bbb "j"))
  (symbol "𝕜" (math-insert-alphabet 'math-alpha-bbb "k"))
  (symbol "𝕝" (math-insert-alphabet 'math-alpha-bbb "l"))
  (symbol "𝕞" (math-insert-alphabet 'math-alpha-bbb "m"))
  (symbol "𝕟" (math-insert-alphabet 'math-alpha-bbb "n"))
  (symbol "𝕠" (math-insert-alphabet 'math-alpha-bbb "o"))
  (symbol "𝕡" (math-insert-alphabet 'math-alpha-bbb "p"))
  (symbol "𝕢" (math-insert-alphabet 'math-alpha-bbb "q"))
  (symbol "𝕣" (math-insert-alphabet 'math-alpha-bbb "r"))
  (symbol "𝕤" (math-insert-alphabet 'math-alpha-bbb "s"))
  (symbol "𝕥" (math-insert-alphabet 'math-alpha-bbb "t"))
  (symbol "𝕦" (math-insert-alphabet 'math-alpha-bbb "u"))
  (symbol "𝕧" (math-insert-alphabet 'math-alpha-bbb "v"))
  (symbol "𝕨" (math-insert-alphabet 'math-alpha-bbb "w"))
  (symbol "𝕩" (math-insert-alphabet 'math-alpha-bbb "x"))
  (symbol "𝕪" (math-insert-alphabet 'math-alpha-bbb "y"))
  (symbol "𝕫" (math-insert-alphabet 'math-alpha-bbb "z"))
  (symbol "𝔸" (math-insert-alphabet 'math-alpha-bbb "A"))
  (symbol "𝔹" (math-insert-alphabet 'math-alpha-bbb "B"))
  (symbol "ℂ" (math-insert-alphabet 'math-alpha-bbb "C"))
  (symbol "𝔻" (math-insert-alphabet 'math-alpha-bbb "D"))
  (symbol "𝔼" (math-insert-alphabet 'math-alpha-bbb "E"))
  (symbol "𝔽" (math-insert-alphabet 'math-alpha-bbb "F"))
  (symbol "𝔾" (math-insert-alphabet 'math-alpha-bbb "G"))
  (symbol "ℍ" (math-insert-alphabet 'math-alpha-bbb "H"))
  (symbol "𝕀" (math-insert-alphabet 'math-alpha-bbb "I"))
  (symbol "𝕁" (math-insert-alphabet 'math-alpha-bbb "J"))
  (symbol "𝕂" (math-insert-alphabet 'math-alpha-bbb "K"))
  (symbol "𝕃" (math-insert-alphabet 'math-alpha-bbb "L"))
  (symbol "𝕄" (math-insert-alphabet 'math-alpha-bbb "M"))
  (symbol "ℕ" (math-insert-alphabet 'math-alpha-bbb "N"))
  (symbol "𝕆" (math-insert-alphabet 'math-alpha-bbb "O"))
  (symbol "ℙ" (math-insert-alphabet 'math-alpha-bbb "P"))
  (symbol "ℚ" (math-insert-alphabet 'math-alpha-bbb "Q"))
  (symbol "ℝ" (math-insert-alphabet 'math-alpha-bbb "R"))
  (symbol "𝕊" (math-insert-alphabet 'math-alpha-bbb "S"))
  (symbol "𝕋" (math-insert-alphabet 'math-alpha-bbb "T"))
  (symbol "𝕌" (math-insert-alphabet 'math-alpha-bbb "U"))
  (symbol "𝕍" (math-insert-alphabet 'math-alpha-bbb "V"))
  (symbol "𝕎" (math-insert-alphabet 'math-alpha-bbb "W"))
  (symbol "𝕏" (math-insert-alphabet 'math-alpha-bbb "X"))
  (symbol "𝕐" (math-insert-alphabet 'math-alpha-bbb "Y"))
  (symbol "ℤ" (math-insert-alphabet 'math-alpha-bbb "Z")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Semantic math menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-correct-menu
  ("Correct all" (math-correct-all))
  ---
  (group "Options")
  ("Remove superfluous invisible operators"
   (toggle-preference "manual remove superfluous invisible"))
  ("Insert missing invisible operators"
   (toggle-preference "manual insert missing invisible"))
  ("Homoglyph substitutions"
   (toggle-preference "manual homoglyph correct")))

(menu-bind context-preferences-menu
  ("Show full context" (toggle-preference "show full context"))
  (when (inside? 'table)
    ("Show table cells" (toggle-preference "show table cells")))
  ("Show current focus" (toggle-preference "show focus"))
  (when (!= (get-preference "semantic editing") "off")
    ("Only show semantic focus"
      (toggle-preference "show only semantic focus"))))

(menu-bind semantic-math-preferences-menu
  ("Semantic editing" (toggle-preference "semantic editing"))
  (when (== (get-preference "semantic editing") "on")
    ("Semantic selections" (toggle-preference "semantic selections")))
  ("Semantic correctness" (toggle-preference "semantic correctness")))

(menu-bind semantic-annotation-menu
  ("Ordinary symbol" (make 'math-ordinary))
  ("Ignore" (make 'math-ignore))
  ---
  ("Separator" (make 'math-separator))
  ("Quantifier" (make 'math-quantifier))
  ("Logical implication" (make 'math-imply))
  ("Logical or" (make 'math-or))
  ("Logical and" (make 'math-and))
  ("Logical not" (make 'math-not))
  ("Relation" (make 'math-relation))
  ("Set union" (make 'math-union))
  ("Set intersection" (make 'math-intersection))
  ("Set difference" (make 'math-exclude))
  ("Addition" (make 'math-plus))
  ("Subtraction" (make 'math-minus))
  ("Multiplication" (make 'math-times))
  ("Division" (make 'math-over))
  ("Prefix" (make 'math-prefix))
  ("Postfix" (make 'math-postfix))
  ("Open" (make 'math-open))
  ("Close" (make 'math-close))
  ---
  ("Other" (make 'syntax)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Menu for inserting mathematical markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-insert-menu
  ("Fraction" (make-fraction))
  ("Evaluation bar" (math-evaluation-bar))
  (-> "Combinatorial number"
      ("Binomial coefficient" (make 'binom))
      ("Stirling number of the first kind" (make 'stirling-first))
      ("Stirling number of the second kind" (make 'stirling-second)))
  ("Square root" (make-sqrt))
  ("N-th root" (make-var-sqrt))
  ("Negation" (make-neg))
  ("Tree" (make-tree))
  ---
  (-> "Script"
      ("Left subscript" (make-script #f #f))
      ("Left superscript" (make-script #t #f))
      ("Right subscript" (make-script #f #t))
      ("Right superscript" (make-script #t #t))
      ("Script below" (make-below))
      ("Script above" (make-above)))
  (-> "Accent above"
      ("Tilda" (make-wide "̃"))
      ("Hat" (make-wide "̂"))
      ("Bar" (make-wide "̅"))
      ("Vector" (make-wide "⃗"))
      ("Check" (make-wide "̌"))
      ("Breve" (make-wide "̆"))
      ("Inverted breve" (make-wide "̑"))
      ---
      ("Acute" (make-wide "́"))
      ("Grave" (make-wide "̀"))
      ("Dot" (make-wide "̇"))
      ("Two dots" (make-wide "̈"))
      ("Three dots" (make-wide "⃛"))
      ("Four dots" (make-wide "⃜"))
      ("Circle" (make-wide "̊"))
      ---
      ("Overbrace" (make-wide-stretched "⏞"))
      ("Underbrace" (make-wide-stretched "⏟"))
      ("Round overbrace" (make-wide-stretched "⏜"))
      ("Round underbrace" (make-wide-stretched "⏝"))
      ("Square overbrace" (make-wide-stretched "⎴"))
      ("Square underbrace" (make-wide-stretched "⎵"))
      ---
      ("Right arrow" (make-wide-stretched "⃗"))
      ("Left arrow" (make-wide-stretched "⃖"))
      ("Left-right arrow" (make-wide-stretched "⃡"))
      ("Wide bar" (make-wide-stretched "̅")))
  (-> "Accent below"
      ("Tilda" (make-wide-under "̃"))
      ("Hat" (make-wide-under "̂"))
      ("Bar" (make-wide-under "̅"))
      ("Vector" (make-wide-under "⃗"))
      ("Check" (make-wide-under "̌"))
      ("Breve" (make-wide-under "̆"))
      ("Inverted breve" (make-wide-under "̑"))
      ---
      ("Acute" (make-wide-under "́"))
      ("Grave" (make-wide-under "̀"))
      ("Dot" (make-wide-under "̇"))
      ("Two dots" (make-wide-under "̈"))
      ("Three dots" (make-wide-under "⃛"))
      ("Four dots" (make-wide-under "⃜"))
      ("Circle" (make-wide-under "̊"))
      ---
      ("Overbrace" (make-wide-under-stretched "⏞"))
      ("Underbrace" (make-wide-under-stretched "⏟"))
      ("Round overbrace" (make-wide-under-stretched "⏜"))
      ("Round underbrace" (make-wide-under-stretched "⏝"))
      ("Square overbrace" (make-wide-under-stretched "⎴"))
      ("Square underbrace" (make-wide-under-stretched "⎵"))
      ---
      ("Right arrow" (make-wide-under-stretched "⃗"))
      ("Left arrow" (make-wide-under-stretched "⃖"))
      ("Left-right arrow" (make-wide-under-stretched "⃡"))
      ("Wide bar" (make-wide-under-stretched "̅")))
  (-> "Symbol" (link symbol-menu))
  (-> "Textual operator" (link textual-operator-menu))
  (if (== (get-preference "semantic editing") "on")
      (-> "Semantics" (link semantic-annotation-menu)))
  ---
  (-> "Content tag" (link math-content-tag-menu))
  (-> "Size tag" (link size-tag-menu))
  (-> "Presentation tag" (link math-presentation-tag-menu)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The Mathematics menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-menu
  (link math-insert-menu)
  ---
  (link texmacs-insert-menu))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Icons for modifying mathematical text properties
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-format-icons
  /
  (=> (balloon (icon "tm_color") "Select a foreground color")
      (link color-menu))
  (=> (balloon (icon "tm_math_style")
               "Change the style of mathematical formulas")
      (group "Style")
      ("Small inline" (make-with "math-display" "false"))
      ("Large displayed" (make-with "math-display" "true"))
      ---
      (group "Size")
      ("Normal" (make-with "math-level" "0"))
      ("Script size" (make-with "math-level" "1"))
      ("Script script size" (make-with "math-level" "2"))
      ---
      (group "Spacing")
      ("Normal" (make-with "math-condensed" "false"))
      ("Condensed" (make-with "math-condensed" "true"))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Icons for inserting mathematical markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-insert-icons
  (=> (balloon (icon "tm_fraction") "Insert a fraction")
      ("Standard fraction" (make-fraction))
      ("Small inline fraction" (make 'tfrac))
      ("Large displayed fraction" (make 'dfrac))
      ("Slashed fraction" (make 'frac*))
      ("Continued fraction" (make 'cfrac)))
  (=> (balloon (icon "tm_root") "Insert a root")
      ("Square root" (make-sqrt))
      ("Multiple root" (make-var-sqrt)))
  (=> (balloon (icon "tm_subsup") "Insert a script")
      ("Subscript" (make-script #f #t))
      ("Superscript" (make-script #t #t))
      ("Left subscript" (make-script #f #f))
      ("Left superscript" (make-script #t #f))
      ("Subscript below" (make-below))
      ("Superscript above" (make-above)))
  /
  (=> (balloon (icon "tm_bigop") "Insert a big operator")
      (tile 6 (link big-operator-menu)))
  (=> (balloon (icon "tm_bigaround") "Insert large delimiters")
      (tile 8 (link large-delimiter-menu))
      ---
      (-> "Opening" (tile 8 (link left-delimiter-menu)))
      (-> "Middle" (tile 8 (link middle-delimiter-menu)))
      (-> "Closing" (tile 8 (link right-delimiter-menu))))
  (=> (balloon (icon "tm_wide") "Insert an accent")
      (tile 6
            ((icon "tm_hat") (make-wide "̂"))
            ((icon "tm_tilda") (make-wide "̃"))
            ((icon "tm_bar") (make-wide "̅"))
            ((icon "tm_vect") (make-wide "⃗"))
            ((icon "tm_check") (make-wide "̌"))
            ((icon "tm_breve") (make-wide "̆"))
            ((icon "tm_invbreve") (make-wide "̑"))
            ((icon "tm_dot") (make-wide "̇"))
            ((icon "tm_ddot") (make-wide "̈"))
            ((icon "tm_acute") (make-wide "́"))
            ((icon "tm_grave") (make-wide "̀"))))
  /
  (=> (balloon (icon "tm_binop") "Insert a binary operation")
      (tile 8 (link binary-operation-menu)))
  (=> (balloon (icon "tm_binrel") "Insert a binary relation")
      (tile 8 (link binary-relation-menu-1))
      ---
      (tile 8 (link binary-relation-menu-2)))
  (=> (balloon (icon "tm_arrow") "Insert an arrow")
      (tile 9 (link horizontal-arrow-menu))
      ---
      (tile 8 (link vertical-arrow-menu))
      ---
      (tile 6 (link long-arrow-menu))
      ---
      (link extensible-arrow-menu))
  (=> (balloon (icon "tm_unequal") "Insert a negation")
      (tile 9 (link negation-menu-1))
      ---
      (tile 9 (link negation-menu-2)))
  (=> (balloon (icon "tm_miscsymb") "Insert a miscellaneous symbol")
      (tile 8 (link miscellaneous-symbol-menu))
      ---
      (tile 6 (link dots-menu)))
  /
  (=> (balloon (icon "tm_greek_char") "Insert a greek character")
      (tile 8 (link lower-greek-menu))
      ---
      (tile 8 (link upper-greek-menu)))
  (=> (balloon (icon "tm_mathbold")
               "Insert a bold character")
      (tile 15 (link bold-num-menu))
      ---
      (tile 13 (link bold-alpha-menu))
      ---
      (tile 13 (link bold-up-alpha-menu))
      ---
      (tile 15 (link bold-greek-menu)))
  (=> (balloon (icon "tm_cal")
               "Insert a calligraphic character")
      (tile 13 (link cal-menu)))
  (=> (balloon (icon "tm_frak")
               "Insert a fraktur character")
      (tile 13 (link frak-menu)))
  (=> (balloon (icon "tm_bbb")
               "Insert a blackboard bold character")
      (tile 13 (link bbb-menu)))
  (=> (balloon (icon "tm_op") "Insert a textual operator")
      (link textual-operator-menu))
  (link math-format-icons)
  (=> (balloon (icon "tm_math_preferences")
               "Preferences for editing mathematical formulas")
      (group "Keyboard")
      ("Enforce brackets to match" (toggle-matching-brackets))
      ("Use extensible brackets" (toggle-preference "use large brackets"))
      ---
      (group "Context aids")
      (link context-preferences-menu)
      ---
      (group "Semantics")
      (link semantic-math-preferences-menu))
  (if (== (get-preference "semantic editing") "on")
      (=> (balloon (icon "tm_math_syntax")
                   "Specify semantics of a symbol or formula")
          (link semantic-annotation-menu))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Icons for math mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind math-icons
  (link math-insert-icons)
  (link texmacs-insert-icons)
  (if (and (in-presentation?) (not (visible-icon-bar? 0)))
    /
    (link dynamic-icons)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Math focus menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (standard-options l)
  (:require (in? l '(math equation equation* eqnarray eqnarray*)))
  (list :recurse "number-long-article" "math-check"))

(tm-define (standard-options l)
  (:require (== l 'math-colored))
  (list "math-ss"))

(tm-define (focus-tag-name l)
  (:require (== l 'math))
  "Inline formula")

(tm-define (focus-tag-name l)
  (:require (in? l '(equation equation*)))
  "Displayed formula")

(tm-define (focus-tag-name l)
  (:require (in? l '(eqnarray eqnarray*)))
  "Equations")

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(math equation equation*)))
  '(formula equation))

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(eqnarray eqnarray*)))
  '(eqnarray*))

(tm-menu (focus-variant-menu t)
  (:require (tree-in? t '(math equation equation*)))
  ("Inline formula" (variant-formula t))
  ("Displayed formula" (variant-equation t)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Script focus menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-can-insert-remove? t)
  (:require (script-context? t))
  #t)

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(lsub lsup)))
  '(lsub lsup))

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(rsub rsup)))
  '(rsub rsup))

(tm-menu (focus-variant-menu t)
  (:require (tree-in? t '(lsub lsup)))
  (when (script-only-script? t)
    ("Left subscript" (variant-set (focus-tree) 'lsub))
    ("Left superscript" (variant-set (focus-tree) 'lsup))))

(tm-menu (focus-variant-menu t)
  (:require (tree-in? t '(rsub rsup)))
  (when (script-only-script? t)
    ("Subscript" (variant-set (focus-tree) 'rsub))
    ("Superscript" (variant-set (focus-tree) 'rsup))))

(tm-menu (focus-insert-menu t)
  (:require (script-context? t))
  (assuming (tree-in? t '(lsub rsub))
    (when (script-only-script? t)
      ("Insert superscript" (structured-insert-up))))
  (assuming (tree-in? t '(lsup rsup))
    (when (script-only-script? t)
      ("Insert subscript" (structured-insert-down)))))

(tm-menu (focus-insert-icons t)
  (:require (script-context? t))
  (assuming (tree-in? t '(lsub rsub))
    (when (script-only-script? t)
      ((balloon (icon "tm_insert_up") "Insert superscript")
       (structured-insert-up))))
  (assuming (tree-in? t '(lsup rsup))
    (when (script-only-script? t)
      ((balloon (icon "tm_insert_down") "Insert subscript")
       (structured-insert-down)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Root focus menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-can-insert-remove? t)
  (:require (tree-is? t 'sqrt))
  #f)

(tm-menu (focus-toggle-menu t)
  (:require (tree-is? t 'sqrt))
  ((check "Multiple root" "v"
          (== (tree-arity (focus-tree)) 2))
   (sqrt-toggle (focus-tree))))

(tm-menu (focus-toggle-icons t)
  (:require (tree-is? t 'sqrt))
  ((check (balloon (icon "tm_root_index") "Multiple root") "v"
          (== (tree-arity (focus-tree)) 2))
   (sqrt-toggle (focus-tree))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Wide accent focus menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-tag-name l)
  (:require (in? l '(wide wide*)))
  "Wide")

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(wide wide*)))
  '(wide))

(tm-menu (focus-toggle-menu t)
  (:require (tree-in? t '(wide wide*)))
  ((check "Accent below" "v"
          (alternate-second? (focus-tree)))
   (alternate-toggle (focus-tree))))

(tm-menu (focus-toggle-icons t)
  (:require (tree-in? t '(wide wide*)))
  ((check (balloon (icon "tm_wide_under") "Accent below") "v"
          (alternate-second? (focus-tree)))
   (alternate-toggle (focus-tree))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Around focus menus
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-has-preferences? t)
  (:require (tree-in? t '(around around*)))
  #t)

(tm-define (standard-options l)
  (:require (in? l '(around around*)))
  (list "math-brackets"))

(tm-define (focus-tag-name l)
  (:require (in? l '(around around*)))
  "Around")

(tm-define (focus-variants-of t)
  (:require (tree-in? t '(around around*)))
  '(around))

(tm-menu (focus-toggle-menu t)
  (:require (tree-in? t '(around around*)))
  ((check "Large brackets" "v"
          (alternate-second? (focus-tree)))
   (alternate-toggle (focus-tree))))

(tm-menu (focus-extra-menu t)
  (:require (tree-in? t '(left mid right around around*)))
  ---
  ("Increase size" (geometry-up))
  ("Decrease size" (geometry-down))
  ("Default size" (geometry-reset)))

(tm-menu (focus-toggle-icons t)
  (:require (tree-in? t '(around around*)))
  ((check (balloon (icon "tm_large_around") "Large brackets") "v"
          (alternate-second? (focus-tree)))
   (alternate-toggle (focus-tree)))
  ;; TODO: create suitable icons
  ;;((balloon (icon "tm_plus") "Increase bracket size")
  ;; (geometry-up))
  ;;((balloon (icon "tm_minus") "Decrease bracket size")
  ;; (geometry-down))
  ;;((balloon (icon "tm_reset") "Reset to default bracket size")
  ;; (geometry-reset))
  )
