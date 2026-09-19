
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : document-style.scm
;; DESCRIPTION : management of global document style
;; COPYRIGHT   : (C) 2001--2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic document-style))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Relations between style files and packages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Menu names of style files and packages, and balloon help
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-table style-synopsis)
(define-table style-menu-name)

(tm-define (style-get-documentation style)
  (with doc (ahash-ref style-synopsis style)
    (and doc (nnull? doc) (car doc))))

(tm-define (style-get-menu-name style)
  (with doc (ahash-ref style-menu-name style)
    (if (and doc (nnull? doc)) (car doc)
        (upcase-first style))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Getting and setting the list of style packages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; High level routines for style and style package management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (set-no-style)
  (:check-mark "v" has-no-style?))

(tm-define (set-main-style style)
  (:synopsis* "Set main document style")
  (:argument style "Style")
  (:default  style "generic")
  (:check-mark "v" has-main-style?)
  (:balloon style-get-documentation)
  (let* ((old (get-style-list))
         (new (if (null? old) (list style) (cons style (cdr old)))))
    (set-style-list new))
  (delayed
    (:idle 1)
    (notify-new-style style)))

(define (custom-style-file-name name)
  (let* ((tail (url->system (url-tail name))))
    (if (string-ends? tail ".ts")
        (string-drop-right tail 3)
        tail)))

(tm-define (install-custom-style name)
  (:synopsis* "Install custom document style")
  (let* ((style-name (custom-style-file-name name))
         (dest-dir   (url-append "$ATHENA_HOME_PATH" "styles"))
         (dest       (url-append dest-dir
                                 (string-append style-name ".ts"))))
    (cond ((not (string-ends? (url->system (url-tail name)) ".ts"))
           (show-message "Please select a TeXmacs stylesheet file ending in .ts."
                         "Install custom style"))
          (else
           (system-mkdir dest-dir)
           (system-copy name dest)
           (style-clear-cache)
           (set-main-style style-name)
           (show-message
            (string-append "Installed and activated style: " style-name)
            "Install custom style")))))

(tm-define (choose-and-install-custom-style)
  (:synopsis* "Choose and install custom document style")
  (choose-file install-custom-style "Install custom style" ""))

(tm-property (add-style-package pack)
  (:synopsis* "Add style package")
  (:argument pack "Package")
  (:check-mark "v" has-style-package?)
  (:balloon style-get-documentation))

(tm-property (remove-style-package pack)
  (:argument pack "Remove package")
  (:proposals pack (with l (get-style-list) (if (null? l) l (cdr l))))
  (:balloon style-get-documentation))

(tm-property (remove-style-package* pack)
  (:argument pack "Remove package")
  (:check-mark "v" not-has-style-package?))

(tm-property (toggle-style-package pack)
  (:argument pack "Toggle package")
  (:check-mark "v" has-style-package?)
  (:balloon style-get-documentation))

(define (url-resolve-package name)
  (let* ((style-name  (string-append name ".ts"))
         (style-url   (url-append "$ATHENA_STYLE_PATH" style-name))
         (style-local (url-relative (current-buffer) style-name)))
    ;; we give precedence to the local style file to a global style with same name     
    (url-resolve (url-or style-local style-url) "r")))

(tm-define (edit-package-source name)
  (with file-name (url-resolve-package name)
    (cursor-history-add (cursor-path))
    (load-document file-name)
    (cursor-history-add (cursor-path))))

(tm-define (edit-style-source)
  (with l (get-style-list)
    (when (and (nnull? l) (string? (car l)))
      (edit-package-source (car l)))))

(tm-define (make* l name)
  (if (or (has-style-package? name)
	  (style-has? (string-append name "-package")))
      (make l)
      (begin
	(add-style-package name)
	(delayed
	  (:idle 1)
	  (make l)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Table with menu names for style packages which are used as style options
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-table style-menu-name
  ("framed-title"         "Framed titles")
  ("title-bar"            "Title bars")
  ("math-ss"              "Sans serif formulas")

  ("centered-program"     "Centered programs")
  ("framed-program"       "Framed programs")
  ("compact-list"         "Compact lists")
  ("triangle-list"        "Triangular list items")
  ("prefix-enumerations"  "Prefix nested numbers")
  ("math-brackets"        "Color according to nesting level")
  ("math-check"           "Highlight errors")
  ("framed-theorems"      "Framed theorems")
  ("hanging-theorems"     "Hanging theorems")
  ("number-europe"        "European numbering style")
  ("number-us"            "US numbering style")
  ("number-long-article"  "Prefix by section number")
  ("captions-above"       "Captions above")

  ("normal-spacing"       "Default spacing")
  ("wide-spacing"         "Wide spacing")
  ("invisible-multiply"   "Invisible multiplications")
  ("narrow-multiply"      "Narrow multiplications")
  ("regular-multiply"     "Regular multiplications")
  ("invisible-apply"      "Invisible function applications")
  ("narrow-apply"         "Narrow function applications")
  ("regular-apply"        "Regular function applications"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Table with brief descriptions for common styles and style packages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-table style-synopsis
  ("article"        "Default style for writing articles")
  ("beamer"         "Style for laptop presentations")
  ("book"           "Default style for writing books")
  ("generic"        "Default document style")
  ("letter"         "Default style for writing letters")
  ("poster"         "Style for posters")
  ("seminar"        "Style for presentations using an overhead projector")
  ("source"         "Style for editing style files and packages")

  ("acmart"         "ACM article style")
  ("acmsmall"       "Small ACM journal style")
  ("acmlarge"       "Large ACM journal style")
  ("acmtog"         "Two column ACM journal style")
  ("sigconf"        "ACM SIGSAM conference style")
  ("sigchi"         "ACM SIGSAM abstract style")
  ("sigplan"        "ACM SIGSAM proceedings style")
  ("amsart"         "AMS article style")
  ("elsarticle"     "Elsevier article style")
  ("ifac"           "IFAC article style")
  ("ieeeconf"       "IEEE conference style")
  ("ieeetran"       "Style for transactions by the IEEE")
  ("aip"            "REVTeX meta-style (American Institute of Physics)")
  ("aps"            "REVTeX meta-style (American Physical Society)")
  ("llncs"          "Style for Springer Lecture Notes in Computer Science")
  ("svjour"         "Article style for Springer journals")
  ("tmarticle"      "TeXmacs alternative article style")

  ("svmono"         "Style for Springer monographs")
  ("tmbook"         "TeXmacs alternative book style")

  ("manual"         "Style for writing technical manuals")
  ("tmdoc"          "Style for writing TeXmacs documentation")
  ("tmmanual"       "Style for writing TeXmacs manuals"))

(define-table style-synopsis
  ("alt-colors"         "Color formulas and several other basic tags")
  ("framed-envs"        "Display various environments inside wide frames")
  ("ornaments"          "Tags for various fancy ornaments")
  ("presentation"       "Base package for laptop presentations")
  ("bluish"             "Bluish beamer theme")
  ("ice"                "Ice beamer theme")
  ("metal"              "Metallic beamer theme")
  ("reddish"            "Reddish beamer theme")
  ("ridged-paper"       "Ridged paper beamer theme")
  ("framed-title"       "Put titles of slides in wide frames")
  ("title-bar"          "Put titles of slides in bar at extreme top of screen")
  ("math-ss"            "Use sans serif font for mathematical formulas")
  
  ("a0-poster"          "A0 page size for posters")
  ("a1-poster"          "A1 page size for posters")
  ("a2-poster"          "A2 page size for posters")
  ("a3-poster"          "A3 page size for posters")
  ("a4-poster"          "A4 page size for posters")
  ("landscape-poster"   "Landscape orientation for posters")
  ("portrait-poster"    "Portrait orientation for posters")
  
  ("centered-program"   "Use a centered rendering style for algorithms")
  ("framed-program"     "Display algorithms inside frames and center")
  ("two-columns"        "Markup and adjustments for two column documents")
  ("compact-list"       "Less indentation and vertical spacing for lists")
  ("triangle-list"      "Use triangular lists items")
  ("prefix-enumerations" "Prefix numbers of nested enumerations")
  ("math-brackets"      "Indicate bracket nesting level using colors")
  ("math-check"         "Highlight mathematical formulas with syntax errors")
  ("framed-theorems"    "Display enunciations inside wide frames")
  ("hanging-theorems"   "Use hanging frames for enunciation titles")
  ("number-europe"      "Individual counters for theorems, propositions, etc.")
  ("number-long-article" "Prefix numbered environments by section number")
  ("number-us"          "Shared counter for theorems, propositions, etc.")
  ("captions-above"     "Place captions above figures and tables")

  ("doc"                "Rich collection of markup for writing documentation")

  ("bpr"                "Example macro package for Basu/Pollack/Roy book")
  ("structured-list"    "Making item bodies part of item tags")
  ("structured-section" "Making section bodies part of section tags")

  ("normal-spacing"     "Default spacing")
  ("wide-spacing"       "Wide spacing")
  ("invisible-multiply" "Use invisible space for multiplications")
  ("narrow-multiply"    "Use narrow space for multiplications")
  ("regular-multiply"   "Use regular space for multiplications")
  ("invisible-apply"    "Use invisible space for function applications")
  ("narrow-apply"       "Use narrow space for function applications")
  ("regular-apply"      "Use regular space for function applications"))
