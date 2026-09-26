
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : text-kbd.scm
;; DESCRIPTION : keystrokes in text mode
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (text text-kbd)
  (:use (generic generic-kbd)
	(utils edit auto-close)
	(text text-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special symbols in text mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-text?)
  ("\"" (insert-quote))
  ("\" var" "\"")
  ("- var" (make 'nbhyph))
  ("<" "<")
  (">" ">")
  ("< var" "")
  ("> var" "")
  ("`" "<#2018>")
  ("'" (insert-apostrophe #f))
  ("` var" "`")
  ("' var" (insert-apostrophe #t))

  ("< <" "")
  ("< < var" "<<")
  ("> >" "")
  ("> > var" ">>")
  ("' '" "")
  ("' ' var" "''")
  ("` `" "")
  ("` ` var" "``")
  (", ," "")
  (", , var" ",,")
  ("- -" "")
  ("- - var" "--")
  ("- - var var" (make 'strike-through))
  ("- - -" "")
  ("- - - var" "---")
  ("- - - -" (make 'hrule))
  ("- - - - var" "----")
  ("- - - - -" "-----")
  (". ." "..")
  (". . var" (make 'fill-out))
  (". . var var" (make 'fill-out*))
  (". . ." (make 'text-dots))
  (". . . var" "...")
  (". . . ." (make 'fill-dots))
  (". . . . var" "....")
  (". . . . ." ".....")
  ("_ _" "__")
  ("_ _ var" (make 'underline))
  ("/ /" "//")
  ("/ / var" (make 'deleted))

  ;; Markdown Style Shortcuts
  ("# # var" (make 'section))
  ("# # # var" (make 'subsection))
  ("# # # # var" (make 'subsubsection))
  ("` ` ` var" (make 'verbatim-code))
  ("* * var" (make-with "font-series" "bold"))
  ("* var" (make-with "font-shape" "italic"))
  ("+ var" (make-tmlist 'itemize))
  ("1 . var" (make-tmlist 'enumerate))

  ("space var" (make 'nbsp))
  ("space var var" (make-space "1em"))
  ("_" "_")
  ("_ var" (make-script #f #t))
  ("^" "^")
  ("^ var" (make-script #t #t))
  ("accent:deadhat var" (make-script #t #t))
  ("text:symbol m" (make 'masculine))
  ("text:symbol M" (make 'varmasculine))
  ("text:symbol f" (make 'ordfeminine))
  ("text:symbol F" (make 'varordfeminine))

  ("" "")
  ("" "")
  ("" "")
  ("" "")
  ("" "")
  ("" "")
  ("" "")
  ("" "")
  ("" "")

  ("exclamdown" "½")
  ("cent" "<#A2>")
  ("sterling" "¿")
  ("currency" "<#A4>")
  ("yen" "<#A5>")
  ("section" "Ÿ")
  ("copyright" "<#A9>")
  ("copyright var" (make 'copyleft))
  ("ordfeminine" "<#AA>")
  ("ordfeminine var" (make 'varordfeminine))
  ("guillemotleft" "")
  ("registered" "<#AE>")
  ("circledR" "<#AE>")
  ("degree" "<#B0>")
  ("twosuperior" "<#B2>")
  ("threesuperior" "<#B3>")
  ("paragraph" "<#B6>")
  ("onesuperior" "<#B9>")
  ("masculine" "<#BA>")
  ("masculine var" (make 'varmasculine))
  ("guillemotright" "")
  ("onequarter" "<#BC>")
  ("onehalf" "<#BD>")
  ("threequarters" "<#BE>")
  ("questiondown" "¾")

  ("trademark" "<#2122>")
  ("big-sum" (insert '(big "sum")))
  ("dagger" "<dag>")
  ("sqrt" (insert '(sqrt "")))
  ("big-int" (insert '(big "int")))
  ("euro" "<#20AC>")
  ("ddagger" "<ddag>")
  ("centerdot" "<cdot>")
  ("big-prod" (insert '(big "prod")))
  ("varspace" (insert '(nbsp))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Font dependent shortcuts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-text?)
  (:require (== (get-env "font") "roman"))

  ("copyright" (make 'copyright)) ;; "<#A9>"
  ("ordfeminine" (make 'ordfeminine))
  ("masculine" (make 'masculine))
  ("<#20AC>" (make 'euro))
  ("euro" (make 'euro)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Greek symbols
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  ("alpha" "<#3B1>")
  ("beta" "<#3B2>")
  ("gamma" "<#3B3>")
  ("delta" "<#3B4>")
  ("epsilon" "<#3F5>")
  ("varepsilon" "<#3B5>")
  ("zeta" "<#3B6>")
  ("eta" "<#3B7>")
  ("theta" "<#3B8>")
  ("vartheta" "<#3D1>")
  ("iota" "<#3B9>")
  ("kappa" "<#3BA>")
  ("varkappa" "<#3F0>")
  ("lambda" "<#3BB>")
  ("mu" "<#3BC>")
  ("nu" "<#3BD>")
  ("xi" "<#3BE>")
  ("omicron" "<#3BF>")
  ("pi" "<#3C0>")
  ("varpi" "<#3D6>")
  ("rho" "<#3C1>")
  ("varrho" "<#3F1>")
  ("sigma" "<#3C3>")
  ("varsigma" "<#3C2>")
  ("tau" "<#3C4>")
  ("upsilon" "<#3C5>")
  ("phi" "<#3C6>")
  ("varphi" "<#3D5>")
  ("chi" "<#3C7>")
  ("psi" "<#3C8>")
  ("omega" "<#3C9>")
  ("Alpha" "<#391>")
  ("Beta" "<#392>")
  ("Gamma" "<#393>")
  ("Delta" "<#394>")
  ("Epsilon" "<#395>")
  ("Zeta" "<#396>")
  ("Eta" "<#397>")
  ("Theta" "<#398>")
  ("Iota" "<#399>")
  ("Kappa" "<#39A>")
  ("Lambda" "<#39B>")
  ("Mu" "<#39C>")
  ("Nu" "<#39D>")
  ("Xi" "<#39E>")
  ("Omicron" "<#39F>")
  ("Pi" "<#3A0>")
  ("Rho" "<#3A1>")
  ("Sigma" "<#3A3>")
  ("Tau" "<#3A4>")
  ("Upsilon" "<#3A5>")
  ("Phi" "<#3A6>")
  ("Chi" "<#3A7>")
  ("Psi" "<#3A8>")
  ("Omega" "<#3A9>"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Overwrite shortcuts which are inadequate in certain contexts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-variants-disabled?)
  ("- var" (begin (insert "-") (kbd-tab)))
  ("space var" (begin (insert " ") (kbd-tab)))
  ("space var var" (begin (insert " ") (kbd-tab) (kbd-tab))))

(kbd-map
  (:mode in-verbatim?)
  ("space var" (insert-tabstop))
  ("space var var" (begin (insert-tabstop) (insert-tabstop)))
  ("$" (insert "$"))
  ("$ var" (make 'math))
  ("\\" "\\")
  ("\\ var" (make 'hybrid))
  ("\"" "\"")
  ("`" "`")
  ("` var" "<#2018>")
  ("'" "'")
  ("' var" "<#2019>")
  ("< <" "<<")
  ("> >" ">>")
  ("' '" "''")
  ("` `" "``")
  ("- -" "--")
  ("- - -" "---")
  ("< < var" "")
  ("> > var" "")
  ("' ' var" "")
  ("` ` var" "")
  (", , var" "")
  ("- - var" "")
  ("- - - var" ""))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Changing the text format
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-text?)
  ("font ^" (make-script #t #t))
  ("font hat" (make-script #t #t))
  ("font _" (make-script #f #t))
  ("font s" (make-with "font-family" "ss"))
  ("font t" (make-with "font-family" "tt"))
  ("font b" (make-with "font-series" "bold"))
  ("font m" (make-with "font-series" "medium"))
  ("font r" (make-with "font-shape" "right"))
  ("font i" (make-with "font-shape" "italic"))
  ("font l" (make-with "font-shape" "slanted"))
  ("font o" (make 'overline))
  ("font p" (make-with "font-shape" "small-caps"))
  ("font u" (make 'underline)))

(kbd-map
  (:profile macos)
  (:mode in-text?)
  ("macos {" (make-line-with "par-mode" "left"))
  ("macos |" (make-line-with "par-mode" "center"))
  ("macos }" (make-line-with "par-mode" "right"))
  ("macos C-{" (make-line-with "par-mode" "justify")))

(kbd-map
  (:profile windows)
  (:mode in-text?)
  ("windows 1" (make-line-with "par-line-sep" "0fn"))
  ("windows 2" (make-line-with "par-line-sep" "1fn"))
  ("windows 5" (make-line-with "par-line-sep" "0.5fn"))
  ("windows l" (make-line-with "par-mode" "left"))
  ("windows e" (make-line-with "par-mode" "center"))
  ("windows r" (make-line-with "par-mode" "right"))
  ("windows j" (make-line-with "par-mode" "justify"))
  ("windows t" (make 'indent)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Standard markup in text mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-std-text?)
  ("text $" (make-equation*))
  ("text &" (make-eqnarray*))

  ("text a" (make 'abbr))
  ("text d" (make-tmlist 'description))
  ("text e" (make-tmlist 'enumerate))
  ("text i" (make-tmlist 'itemize))
  ("text m" (make 'em))
  ("text n" (make 'name))
  ("text p" (make 'samp))
  ("text s" (make 'strong))
  ("text v" (make 'verbatim))
  ("text ;" (make-item))
  ("text 0" (make-section 'chapter))
  ("text 1" (make-section 'section))
  ("text 2" (make-section 'subsection))
  ("text 3" (make-section 'subsubsection))
  ("text 4" (make-section 'paragraph))
  ("text 5" (make-section 'subparagraph))

  ("F5" (make 'em))
  ("F6" (make 'strong))
  ("F7" (make 'verbatim))
  ("F8" (make 'samp))
  ("S-F6" (make 'name)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Other shortcuts in text mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-std?)
  ("std @" (go-to-section-title)))
