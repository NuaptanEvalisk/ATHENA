
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : math-fonts.scm
;; DESCRIPTION : setup OpenType and Unicode fonts for math mode
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (fonts fonts-math))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; OpenType and Unicode mathematical fonts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(set-font-rules
 '(((unicode-math $up $it $bup $bit $t $a $b $s $d)
    (unimath
     (unicode $up $s $d)
     (unicode $it $s $d)
     (unicode $bup $s $d)
     (unicode $bit $s $d)
     (roman $t $a $b $s $d)))

   ((math-latin-modern $t $a right $s $d)
    (unicode-math latinmodern-math latinmodern-math
                  lmroman10-bold lmroman10-bold
                  $t $a $b $s $d))
   ((math-latin-modern $t $a $b $s $d)
    (unicode-math latinmodern-math lmroman10-italic
                  lmroman10-bold lmroman10-bolditalic
                  $t $a $b $s $d))

   ;;((math-bonum $t bold right $s $d)
   ;; (unicode-math texgyrebonum-bold texgyrebonum-bold
   ;;		  texgyrebonum-bold texgyrebonum-bold
   ;;		  $t $a $b $s $d))
   ((math-bonum $t $a right $s $d)
    (unicode-math texgyrebonum-math texgyrebonum-math
		  texgyrebonum-bold texgyrebonum-bold
		  $t $a $b $s $d))
   ;;((math-bonum $t bold $b $s $d)
   ;; (unicode-math texgyrebonum-bold texgyrebonum-bolditalic
   ;;		  texgyrebonum-bold texgyrebonum-bolditalic
   ;;		  $t $a $b $s $d))
   ((math-bonum $t $a $b $s $d)
    (unicode-math texgyrebonum-math texgyrebonum-italic
		  texgyrebonum-bold texgyrebonum-bolditalic
		  $t $a $b $s $d))

   ;;((math-pagella $t bold right $s $d)
   ;; (unicode-math texgyrepagella-bold texgyrepagella-bold
   ;;		  texgyrepagella-bold texgyrepagella-bold
   ;;		  $t $a $b $s $d))
   ((math-pagella $t $a right $s $d)
    (unicode-math texgyrepagella-math texgyrepagella-math
		  texgyrepagella-bold texgyrepagella-bold
		  $t $a $b $s $d))
   ;;((math-pagella $t bold $b $s $d)
   ;; (unicode-math texgyrepagella-bold texgyrepagella-bolditalic
   ;;		  texgyrepagella-bold texgyrepagella-bolditalic
   ;;		  $t $a $b $s $d))
   ((math-pagella $t $a $b $s $d)
    (unicode-math texgyrepagella-math texgyrepagella-italic
		  texgyrepagella-bold texgyrepagella-bolditalic
		  $t $a $b $s $d))

   ;;((math-schola $t bold right $s $d)
   ;; (unicode-math texgyreschola-bold texgyreschola-bold
   ;;		  texgyreschola-bold texgyreschola-bold
   ;;		  $t $a $b $s $d))
   ((math-schola $t $a right $s $d)
    (unicode-math texgyreschola-math texgyreschola-math
		  texgyreschola-bold texgyreschola-bold
		  $t $a $b $s $d))
   ;;((math-schola $t bold $b $s $d)
   ;; (unicode-math texgyreschola-bold texgyreschola-bolditalic
   ;;		  texgyreschola-bold texgyreschola-bolditalic
   ;;		  $t $a $b $s $d))
   ((math-schola $t $a $b $s $d)
    (unicode-math texgyreschola-math texgyreschola-italic
		  texgyreschola-bold texgyreschola-bolditalic
		  $t $a $b $s $d))

   ;;((math-termes $t bold right $s $d)
   ;; (unicode-math texgyretermes-bold texgyretermes-bold
   ;;		  texgyretermes-bold texgyretermes-bold
   ;;		  $t $a $b $s $d))
   ((math-termes $t $a right $s $d)
    (unicode-math texgyretermes-math texgyretermes-math
		  texgyretermes-bold texgyretermes-bold
		  $t $a $b $s $d))
   ;;((math-termes $t bold $b $s $d)
   ;; (unicode-math texgyretermes-bold texgyretermes-bolditalic
   ;;		  texgyretermes-bold texgyretermes-bolditalic
   ;;		  $t $a $b $s $d))
   ((math-termes $t $a $b $s $d)
    (unicode-math texgyretermes-math texgyretermes-italic
		  texgyretermes-bold texgyretermes-bolditalic
		  $t $a $b $s $d))

   ((math-stix $t bold right $s $d)
    (unicode-math STIX-Bold STIX-Bold
		  STIX-Bold STIX-Bold
		  $t bold $b $s $d))
   ((math-stix $t $a right $s $d)
    (unicode-math STIX-Regular STIX-Regular
		  STIX-Bold STIX-Bold
		  $t $a $b $s $d))
   ((math-stix $t bold $b $s $d)
    (unicode-math STIX-Bold STIX-BoldItalic
		  STIX-Bold STIX-BoldItalic
		  $t bold $b $s $d))
   ((math-stix $t $a $b $s $d)
    (unicode-math STIX-Regular STIX-Italic
		  STIX-Bold STIX-BoldItalic
		  $t $a $b $s $d))

   ((math-asana $t $a $b $s $d)
    (unicode-math Asana-Math Asana-Math
		  Asana-Math Asana-Math
		  $t $a $b $s $d))
   ((math-lucida $t $a $b $s $d)
    (unicode-math LucidaGrande LucidaGrande
		  LucidaGrande LucidaGrande
		  $t $a $b $s $d))
   ((math-apple $t $a $b $s $d)
    (unicode-math #{Apple Symbols}# #{Apple Symbols}#
		  #{Apple Symbols}# #{Apple Symbols}#
		  $t $a $b $s $d))

   ((math-dejavu ms bold right $s $d)
    (unicode-math DejaVuSans-Bold DejaVuSans-Bold
		  DejaVuSans-Bold DejaVuSans-Bold
		  ms bold $b $s $d))
   ((math-dejavu ms $a right $s $d)
    (unicode-math DejaVuSans DejaVuSans
		  DejaVuSans-Bold DejaVuSans-Bold
		  ms $a $b $s $d))
   ((math-dejavu ms bold $b $s $d)
    (unicode-math DejaVuSans-Bold DejaVuSans-BoldOblique
		  DejaVuSans-Bold DejaVuSans-BoldOblique
		  ms bold $b $s $d))
   ((math-dejavu ms $a $b $s $d)
    (unicode-math DejaVuSans DejaVuSans-Oblique
		  DejaVuSans-Bold DejaVuSans-BoldOblique
		  ms $a $b $s $d))

   ((math-dejavu mt bold right $s $d)
    (unicode-math DejaVuSansMono-Bold DejaVuSansMono-Bold
		  DejaVuSansMono-Bold DejaVuSansMono-Bold
		  mt bold $b $s $d))
   ((math-dejavu mt $a right $s $d)
    (unicode-math DejaVuSansMono DejaVuSansMono
		  DejaVuSansMono-Bold DejaVuSansMono-Bold
		  mt $a $b $s $d))
   ((math-dejavu mt bold $b $s $d)
    (unicode-math DejaVuSansMono-Bold DejaVuSansMono-BoldOblique
		  DejaVuSansMono-Bold DejaVuSansMono-BoldOblique
		  mt bold $b $s $d))
   ((math-dejavu mt $a $b $s $d)
    (unicode-math DejaVuSansMono DejaVuSansMono-Oblique
		  DejaVuSansMono-Bold DejaVuSansMono-BoldOblique
		  mt $a $b $s $d))

   ((math-dejavu $t bold right $s $d)
    (unicode-math DejaVuSerif-Bold DejaVuSerif-Bold
		  DejaVuSerif-Bold DejaVuSerif-Bold
		  $t bold $b $s $d))
   ((math-dejavu $t $a right $s $d)
    (unicode-math DejaVuSerif DejaVuSerif
		  DejaVuSerif-Bold DejaVuSerif-Bold
		  $t $a $b $s $d))
   ((math-dejavu $t bold $b $s $d)
    (unicode-math DejaVuSerif-Bold DejaVuSerif-BoldItalic
		  DejaVuSerif-Bold DejaVuSerif-BoldItalic
		  $t bold $b $s $d))
   ((math-dejavu $t $a $b $s $d)
    (unicode-math DejaVuSerif DejaVuSerif-Italic
		  DejaVuSerif-Bold DejaVuSerif-BoldItalic
		  $t $a $b $s $d))))
