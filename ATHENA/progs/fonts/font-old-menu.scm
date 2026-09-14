
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : font-old-menu.scm
;; DESCRIPTION : contextual font menus backed by modern scalable fonts
;; COPYRIGHT   : (C) 1999--2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (fonts font-old-menu)
  (:use (generic format-edit)))

(menu-bind text-font-menu
  (-> "Name"
      ("Roman" (make-with "font" "roman"))
      (if (font-exists-in-tt? "texgyrebonum-regular")
          ("Bonum" (make-with "font" "bonum")))
      (if (font-exists-in-tt? "texgyrepagella-regular")
          ("Pagella" (make-with "font" "pagella")))
      (if (font-exists-in-tt? "texgyreschola-regular")
          ("Schola" (make-with "font" "schola")))
      (if (font-exists-in-tt? "texgyretermes-regular")
          ("Termes" (make-with "font" "termes")))
      (if (font-exists-in-tt? "STIX-Regular")
          ("Stix" (make-with "font" "stix")))
      (if (font-exists-in-tt? "DejaVuSerif")
          ("Dejavu" (make-with "font" "dejavu")))
      ---
      ("Avant Garde" (make-with "font" "avant-garde"))
      ("Bookman" (make-with "font" "bookman"))
      ("Courier" (make-with "font" "courier"))
      ("Helvetica" (make-with "font" "helvetica"))
      ("N.C. Schoolbook" (make-with "font" "new-century-schoolbook"))
      ("Palatino" (make-with "font" "palatino"))
      ("Times" (make-with "font" "times"))
      (if (font-exists-in-tt? "times")
          (-> "Microsoft"
              (if (font-exists-in-tt? "andalemo")
                  ("Andalemo" (make-with "font" "ms-andalemo")))
              (if (font-exists-in-tt? "arial")
                  ("Arial" (make-with "font" "ms-arial")))
              (if (font-exists-in-tt? "comic")
                  ("Comic" (make-with "font" "ms-comic")))
              (if (font-exists-in-tt? "cour")
                  ("Courier" (make-with "font" "ms-courier")))
              (if (font-exists-in-tt? "georgia")
                  ("Georgia" (make-with "font" "ms-georgia")))
              (if (font-exists-in-tt? "impact")
                  ("Impact" (make-with "font" "ms-impact")))
              (if (font-exists-in-tt? "lucon")
                  ("Lucida" (make-with "font" "ms-lucida")))
              (if (font-exists-in-tt? "tahoma")
                  ("Tahoma" (make-with "font" "ms-tahoma")))
              (if (font-exists-in-tt? "times")
                  ("Times" (make-with "font" "ms-times")))
              (if (font-exists-in-tt? "trebuc")
                  ("Trebuchet" (make-with "font" "ms-trebuchet")))
              (if (font-exists-in-tt? "verdana")
                  ("Verdana" (make-with "font" "ms-verdana")))))
      (if (or (supports-chinese?) (supports-japanese?) (supports-korean?))
          (-> "CJK"
              (if (font-exists-in-tt? "Batang")
                  ("Batang" (make-with "font" "batang")))
              (if (font-exists-in-tt? "FandolFang-Regular")
                  ("FandolFang" (make-with "font" "FandolFang")))
              (if (font-exists-in-tt? "FandolHei-Regular")
                  ("FandolHei" (make-with "font" "FandolHei")))
              (if (font-exists-in-tt? "FandolKai-Regular")
                  ("FandolKai" (make-with "font" "FandolKai")))
              (if (font-exists-in-tt? "FandolSong-Regular")
                  ("FandolSong" (make-with "font" "FandolSong")))
              (if (font-exists-in-tt? "MS Gothic")
                  ("MS Gothic" (make-with "font" "ms-gothic")))
              (if (font-exists-in-tt? "MS Mincho")
                  ("MS Mincho" (make-with "font" "ms-mincho")))
              (if (font-exists-in-tt? "simfang")
                  ("SimFang" (make-with "font" "simfang")))
              (if (font-exists-in-tt? "simhei")
                  ("SimHei" (make-with "font" "simhei")))
              (if (font-exists-in-tt? "simkai")
                  ("SimKai" (make-with "font" "simkai")))
              (if (font-exists-in-tt? "simsun")
                  ("SimSun" (make-with "font" "simsun")))
              (if (font-exists-in-tt? "ukai")
                  ("UKai" (make-with "font" "ukai")))
              (if (font-exists-in-tt? "uming")
                  ("UMing" (make-with "font" "uming")))))
      (-> "X-windows"
          ("Times" (make-with "font" "x-times"))
          ("Courier" (make-with "font" "x-courier"))
          ("Helvetica" (make-with "font" "x-helvetica"))
          ("Utopia" (make-with "font" "x-utopia"))
          ("Lucida" (make-with "font" "x-lucida"))))
  (-> "Variant"
      ("Roman" (make-with "font-family" "rm"))
      ("Typewriter" (make-with "font-family" "tt"))
      ("Sans serif" (make-with "font-family" "ss")))
  (-> "Series"
      ("Light" (make-with "font-series" "light"))
      ("Medium" (make-with "font-series" "medium"))
      ("Bold" (make-with "font-series" "bold")))
  (-> "Shape"
      ("Upright" (make-with "font-shape" "right"))
      ("Slanted" (make-with "font-shape" "slanted"))
      ("Italic" (make-with "font-shape" "italic"))
      ("Small caps" (make-with "font-shape" "small-caps")))
  (-> "Size" (link font-size-menu)))

(menu-bind math-font-menu
  (-> "Name"
      ("Roman" (make-with "math-font" "roman"))
      (if (font-exists-in-tt? "texgyrebonum-math")
          ("Bonum" (make-with "math-font" "math-bonum")))
      (if (font-exists-in-tt? "texgyrepagella-math")
          ("Pagella" (make-with "math-font" "math-pagella")))
      (if (font-exists-in-tt? "texgyreschola-math")
          ("Schola" (make-with "math-font" "math-schola")))
      (if (font-exists-in-tt? "texgyretermes-math")
          ("Termes" (make-with "math-font" "math-termes")))
      (if (font-exists-in-tt? "STIX-Regular")
          ("Stix" (make-with "math-font" "math-stix")))
      (if (font-exists-in-tt? "Asana-Math")
          ("Asana" (make-with "math-font" "math-asana")))
      (if (font-exists-in-tt? "DejaVuSerif")
          ("Dejavu" (make-with "math-font" "math-dejavu")))
      (if (font-exists-in-tt? "LucidaGrande")
          ("Lucida" (make-with "math-font" "math-lucida")))
      (if (font-exists-in-tt? "Apple Symbols")
          ("Apple symbols" (make-with "math-font" "math-apple"))))
  (-> "Variant"
      ("Roman" (make-with "math-font-family" "mr"))
      ("Typewriter" (make-with "math-font-family" "mt"))
      ("Sans serif" (make-with "math-font-family" "ms")))
  (-> "Series"
      ("Medium" (make-with "math-font-series" "medium"))
      ("Bold" (make-with "math-font-series" "bold")))
  (-> "Shape"
      ("Default" (make-with "math-font-shape" "normal"))
      ("Right" (make-with "math-font-shape" "right"))
      ("Italic" (make-with "math-font-shape" "italic")))
  (-> "Size" (link font-size-menu)))

(menu-bind prog-font-menu
  (-> "Name"
      ("Roman" (make-with "prog-font" "roman"))
      ("Cursor" (make-with "prog-font" "cursor"))
      (if (font-exists-in-tt? "DejaVuSansMono")
          ("Dejavu Mono" (make-with "prog-font" "dejavu"))))
  (-> "Variant"
      ("Roman" (make-with "prog-font-family" "rm"))
      ("Typewriter" (make-with "prog-font-family" "tt"))
      ("Sans serif" (make-with "prog-font-family" "ss")))
  (-> "Series"
      ("Medium" (make-with "prog-font-series" "medium"))
      ("Bold" (make-with "prog-font-series" "bold")))
  (-> "Shape"
      ("Default" (make-with "prog-font-shape" "normal"))
      ("Right" (make-with "prog-font-shape" "right"))
      ("Italic" (make-with "prog-font-shape" "italic")))
  (-> "Size" (link font-size-menu)))
