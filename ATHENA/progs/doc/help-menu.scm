
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : help-menu.scm
;; DESCRIPTION : the help menu
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (doc help-menu)
  (:use (doc help-funcs))); (doc apidoc)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The Help menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(menu-bind help-menu
  ("About ATHENA" (help-about))
  ---
  (when (url-exists-in-help? "about/welcome/new-welcome.en.tm")
	("Welcome" (load-help-article "about/welcome/new-welcome"))
	("Getting started" (load-help-article "about/welcome/start"))
	---)
  (link athena-help-utilities-menu)
  ---
  (when (url-exists-in-help? "main/config/man-configuration.en.tm")
    (-> "Configuration"
        ("Browse" (load-help-buffer "main/config/man-configuration"))
        ---
        ("Preferences"
         (load-help-article "main/config/man-preferences"))
        ("Keyboard configuration"
         (load-help-article "main/config/man-config-keyboard"))
        ("Users of Cyrillic languages"
         (load-help-article "main/config/man-russian"))
        ("Users of oriental languages"
         (load-help-article "main/config/man-oriental"))))
  (when (url-exists-in-help? "main/man-manual.en.tm")
	(-> "Manual"
	    ("Browse" (load-help-buffer "main/man-manual"))
	    ---
	    ("Preferences"
	     (load-help-article "main/config/man-preferences"))
	    ---
	    ("Getting started"
	     (load-help-article "main/start/man-getting-started"))
	    ("ATHENA knowledge workflows"
	     (load-help-article "main/start/man-athena-workflows"))
	    ("Typing simple texts"
	     (load-help-article "main/text/man-text"))
	    ("Mathematical formulas"
	     (load-help-article "main/math/man-math"))
	    ("Tabular material"
	     (load-help-article "main/table/man-table"))
	    ("Automatic content generation"
	     (load-help-article "main/links/man-links"))
	    ("Namespaces in ATHENA"
	     (load-help-article "main/links/man-namespaces"))
	    ("Creating technical pictures"
	     (load-help-article "main/graphics/man-graphics"))
	    ("Advanced layout features"
	     (load-help-article "main/layout/man-layout"))
            ---
	    ("Editing tools"
	     (load-help-article "main/editing/man-editing-tools"))
	    ("Laptop presentations"
	     (load-help-article "main/beamer/man-beamer"))
	    ("ATHENA as an interface"
	     (load-help-article "main/interface/man-itf"))
            ---
	    ("Writing your own style files"
	     (load-help-article "devel/style/style"))
	    ("Customizing ATHENA"
	     (load-help-article "main/scheme/man-scheme"))
	    ))
  (when (url-exists-in-help? "main/man-manual.en.tm")
	(-> "Reference guide"
	    ("Browse" (load-help-buffer "main/man-reference"))
	    ---
	    ("The ATHENA format"
	     (load-help-article "devel/format/basics/basics"))
	    ("Standard environment variables"
	     (load-help-article "devel/format/environment/environment"))
	    ("ATHENA primitives"
	     (load-help-article "devel/format/regular/regular"))
	    ("Stylesheet language"
	     (load-help-article "devel/format/stylesheet/stylesheet"))
	    ("Standard ATHENA styles"
	     (load-help-article "main/styles/styles"))
	    ("Compatibility with other formats"
	     (load-help-article "main/convert/man-convert"))))

  (when (url-exists-in-help? "about/about.en.tm")
	(-> "Apropos"
	    ("Browse" (load-help-buffer "about/about"))
	    ---
	    ("Summary"
	     (load-help-article "about/about-summary"))
	    ("License"
	     (load-document "$ATHENA_PATH/LICENSE"))
	    ("Philosophy"
	     (load-help-article "about/philosophy/philosophy"))
	    ("The ATHENA authors"
	     (load-help-article "about/authors/authors"))
	    ---
	    ("Original welcome message"
	     (load-help-article "about/welcome/first"))))
  ---
  (-> "Search"
      ("Documentation" (interactive docgrep-in-doc))
      ("Source code" (interactive docgrep-in-src))
      ;;("My documents" (interactive docgrep-in-texts))
      ("Recent documents" (interactive docgrep-in-recent)))
  (-> "Full manuals"
      (when (url-exists-in-help? "main/man-user-manual.en.tm")
        ("User manual" (load-help-book "main/man-user-manual")))
      ;; (when (url-exists-in-help? "tutorial/tut-tutorial.en.tm")
      ;;   ("Tutorial" (load-help-book "tutorial/tut-tutorial")))
      ---
      (when (style-has? "tmdoc-style")
        ("Compile article" (tmdoc-expand-this "article"))
        ("Compile book" (tmdoc-expand-this "book")))))
