
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : preferences-widgets.scm
;; DESCRIPTION : the preferences widgets
;; COPYRIGHT   : (C) 2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus preferences-widgets)
  (:use (athena menus preferences-menu)
        (athena menus view-widgets)
        (fonts font-new-widgets)
        (athena athena tm-vault)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Wrapper
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (set-pretty-preference* which pretty-val)
  (let* ((old (get-preference which))
         (act (set-pretty-preference which pretty-val))
         (new (get-preference which)))
    (when (!= new old)
      (notify-restart))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Appearance preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-preference-names "look and feel"
  ("default" "Default")
  ("emacs" "Emacs")
  ("gnome" "Gnome")
  ("kde" "KDE")
  ("macos" "Mac OS")
  ("windows" "Windows"))

(for (l supported-languages)
  (set-preference-name "language" l (upcase-first l)))

(define-preference-names "complex actions"
  ("menus" "Through the menus")
  ("popups" "Through popup windows"))

(define-preference-names "interactive questions"
  ("footer" "On the footer")
  ("popup" "In popup windows"))

(define-preference-names "detailed menus"
  ("simple ""Simplified menus")
  ("detailed" "Detailed menus"))

(define-preference-names "buffer management"
  ("separate" "Documents in separate windows")
  ("shared" "Multiple documents share window")
  ("mdi" "Multiple documents in sub-windows (MDI)")
  ("ads" "Advanced Docking System"))

(if (qt6-or-later-gui?)
    (define-preference-names "gui theme"
      ("default" "Default")
      ("light" "Bright")
      ("dark" "Dark"))
    (define-preference-names "gui theme"
      ("default" "Default")
      ("light" "Bright")
      ("dark" "Dark")
      ("native-light" "Native")
      ("" "Legacy"))
)

(when (not (qt6-or-later-gui?))
  (when (in? (get-preference "gui theme") '("light" "dark" "default"))
    (set-preference "gui theme" "default")))

(tm-widget (general-basic-preferences-widget)
  (aligned
    (item (text "User interface language:")
      (enum (set-pretty-preference "language" answer)
            (map upcase-first supported-languages)
            (get-pretty-preference "language")
            "18em"))
    (item (text "Complex actions:")
      (enum (set-pretty-preference "complex actions" answer)
            '("Through the menus" "Through popup windows")
            (get-pretty-preference "complex actions")
            "18em"))
    (item (text "Interactive questions:")
      (enum (set-pretty-preference "interactive questions" answer)
            '("On the footer" "In popup windows")
            (get-pretty-preference "interactive questions")
            "18em"))
    (item (text "Details in menus:")
      (enum (set-pretty-preference "detailed menus" answer)
            '("Simplified menus" "Detailed menus")
            (get-pretty-preference "detailed menus")
            "18em"))
    (item (text "Buffer management:")
      (enum (set-pretty-preference "buffer management" answer)
            '("Documents in separate windows"
              "Multiple documents share window"
              "Multiple documents in sub-windows (MDI)"
              "Advanced Docking System")
            (get-pretty-preference "buffer management")
            "18em"))
    (item (text "Automatically save:")
      (enum (set-pretty-preference "autosave" answer)
            '("5 sec" "30 sec" "120 sec" "300 sec" "Disable")
            (get-pretty-preference "autosave")
            "18em"))
    (item (text "Use case-insensitive search:")
      (toggle (set-boolean-preference "case-insensitive-match" answer)
              (get-boolean-preference "case-insensitive-match")))
    (assuming (updater-supported?)
      (item (text "Check for automatic updates:")
        (enum (set-pretty-preference "updater:interval" answer)
              (automatic-checks-choices)
              (get-pretty-preference "updater:interval")
              "18em")))
    (assuming (updater-supported?)
      (item (text "Last check:") (text (last-check-string))))))

(tm-widget (general-appearance-preferences-widget)
  (vertical
    (aligned
      (item (text "Look and feel:")
        (enum (set-pretty-preference* "look and feel" answer)
              '("Default" "Emacs" "Gnome" "KDE" "Mac OS" "Windows")
              (get-pretty-preference "look and feel")
              "18em"))
      (item (text "User interface theme:")
        (enum (set-pretty-preference* "gui theme" answer)
              (if (qt6-or-later-gui?)
                  '("Default" "Bright" "Dark" "")
                  '("Default" "Bright" "Dark" "Native" "Legacy" "")
              )
              (get-pretty-preference "gui theme")
              "18em"))
      (item (text "Use inertial scrolling:")
        (toggle (set-boolean-preference "inertial scrolling" answer)
                (get-boolean-preference "inertial scrolling")))
      (item (text "Inertial momentum (0.80-0.99):")
        (input (set-preference "inertial scrolling friction" answer) "string"
               (list (get-preference "inertial scrolling friction"))
               "10em"))
      (item (text "Inertial sensitivity (multiplier):")
        (input (set-preference "inertial scrolling sensitivity" answer) "string"
               (list (get-preference "inertial scrolling sensitivity"))
               "10em"))
      (item (text "Vault welcome page:")
        (toggle (set-preference "vault welcome page" (if answer "on" "off"))
                (equal? (get-preference "vault welcome page") "on")))
      (item (text "Use multi-tabs:")
        (toggle (set-boolean-preference "enable tab" answer)
                (get-boolean-preference "enable tab")))
      (item (text "Use print dialogue:")
        (toggle (set-boolean-preference "gui:print dialogue" answer)
                (get-boolean-preference "gui:print dialogue")))
      (item (text "Disable window positioning:")
        (toggle (set-boolean-preference "disable texmacs window positioning" answer)
                (get-boolean-preference "disable texmacs window positioning")))
      (item (text "New bibliography dialogue:")
        (toggle (set-boolean-preference "gui:new bibliography dialogue" answer)
                (get-boolean-preference "gui:new bibliography dialogue")))
      (item (text "Show live statistics in central footer:")
        (toggle (set-boolean-preference "gui:live-statistics" answer)
                (get-boolean-preference "gui:live-statistics"))))
    (assuming (> (get-retina-scale) 0)
      (vertical
        ===
        (aligned
          (assuming (and (os-macos?) (qt4-gui?))
            (item (text "Use retina fonts:")
              (toggle (set-retina-boolean-preference "retina-factor" answer)
                      (get-retina-boolean-preference "retina-factor"))))
          (assuming (and (os-macos?) (qt4-gui?))
            (item (text "Use retina icons:")
              (toggle (set-retina-boolean-preference "retina-icons" answer)
                      (get-retina-boolean-preference "retina-icons"))))
          (assuming (and (os-macos?) (qt4-gui?))
            (item (text "Scale graphical interface:")
              (enum (set-retina-preference "retina-scale" answer)
                    '("1" "1.2" "1.4" "1.6" "1.8" "2" "")
                    (get-retina-preference "retina-scale")
                    "5em")))
          (assuming (and (os-macos?) (qt5-or-later-gui?))
            (item (text "Use retina fonts:")
              (toggle (set-retina-boolean-preference "retina-factor" answer)
                      (get-retina-boolean-preference "retina-factor"))))
          (assuming (and (os-macos?) (qt5-or-later-gui?))
            (assuming (!= (get-preference "gui theme") "")
              (item (text "Scale graphical interface:")
                (enum (set-retina-preference "retina-scale" answer)
                      '("1" "1.2" "1.5" "2" "")
                      (get-retina-preference "retina-scale")
                      "5em"))))
          (assuming (not (os-macos?))
            (item (text "Double the zoom factor for TeXmacs documents:")
              (toggle (set-retina-boolean-preference "retina-zoom" answer)
                      (get-retina-boolean-preference "retina-zoom"))))
          (assuming (not (os-macos?))
            (item (text "Use high resolution icons:")
              (toggle (set-retina-boolean-preference "retina-icons" answer)
                      (get-retina-boolean-preference "retina-icons"))))
          (assuming (not (os-macos?))
            (assuming (!= (get-preference "gui theme") "")
              (item (text "Scale of the graphical user interface:")
                (enum (set-retina-preference "retina-scale" answer)
                      '("1" "1.2" "1.5" "2" "")
                      (get-retina-preference "retina-scale")
                      "5em")))))))))

(tm-define (set-user-preferred-fonts l)
  (set-preference "preferred fonts" (object->string l)))

(tm-define (add-user-preferred-font)
  (:interactive #t)
  (interactive (lambda (fam)
                 (when (and fam (!= fam ""))
                   (let* ((cur (get-user-preferred-fonts))
                          (new (if (in? fam cur) cur (append cur (list fam)))))
                     (set-user-preferred-fonts new)
                     (refresh-now "preferred-fonts-list")
                     (update-menus))))
               (cons* "Select a font to add" "string" (font-database-families))))

(tm-define (remove-user-preferred-font fam)
  (let* ((cur (get-user-preferred-fonts))
         (new (list-filter cur (lambda (x) (!= x fam)))))
    (set-user-preferred-fonts new)
    (refresh-now "preferred-fonts-list")
    (update-menus)))
(tm-define (preferred-fonts-list-widget)
  (let ((fonts (get-user-preferred-fonts)))
    (if (null? fonts)
        (list "No preferred fonts added yet.")
        (map (lambda (f)
               (list 'hlist 
                     (list 'text f)
                     '(glue #t #f 15 0)
                     (list "Remove" (lambda () (remove-user-preferred-font f)))))
             fonts))))

(tm-widget (general-fonts-preferences-widget)
  (vertical
    (bold (text "Styling"))
    ===
    (aligned
      (item (text "New style fonts:")
        (toggle (set-boolean-preference "new style fonts" answer)
                (get-boolean-preference "new style fonts")))
      (item (text "Advanced font customization:")
        (toggle (set-boolean-preference "advanced font customization" answer)
                (get-boolean-preference "advanced font customization"))))
    ======
    (bold (text "Preferred fonts"))
    ===
    (refreshable "preferred-fonts-list"
      (dynamic (preferred-fonts-list-widget)))
    ===
    (hlist
      (explicit-buttons ("Add font" (add-user-preferred-font))) >>>)
    ======
    (bold (text "Maintenance"))
    ===
    (aligned
      (item (text "Scan for system fonts:")
        (explicit-buttons ("Scan disk for fonts" (scan-disk-for-fonts))))
      (item (text "Clear local font cache:")
        (explicit-buttons ("Clear font cache" (clear-font-cache)))))))

(tm-widget (general-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "Basic")
        (centered
          (dynamic (general-basic-preferences-widget))))
      (tab (text "Appearance")
        (centered
          (dynamic (general-appearance-preferences-widget))))
      (tab (text "Fonts")
        (centered
          (dynamic (general-fonts-preferences-widget))))))
  ===)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Rendering preferences widget
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (rendering-document-preferences-widget)
  (aligned
    (item (text "Transclusion background:")
      (input (set-preference "vault transclusion color" answer) "string"
             (list (get-preference "vault transclusion color"))
             "10em"))
    (item (text "Cursor color:")
      (input (set-preference "gui cursor color" answer) "string"
             (list (get-preference "gui cursor color"))
             "10em"))
    (item (text "Selection color:")
      (input (set-preference "gui selection color" answer) "string"
             (list (get-preference "gui selection color"))
             "10em"))
    (item (text "Labels display:")
      (enum (set-preference "vault labels mode" answer)
            '("visible" "small" "hidden")
            (get-preference "vault labels mode")
            "10em"))
    (item (text "Persistent fit width:")
      (toggle (set-preference "persistent fit width" (if answer "on" "off"))
              (equal? (get-preference "persistent fit width") "on")))
    (item (text "Alpha transparency:")
      (toggle (set-boolean-preference "experimental alpha" answer)
              (get-boolean-preference "experimental alpha")))
    (item (text "New style page breaking:")
      (toggle (set-boolean-preference "new style page breaking" answer)
              (get-boolean-preference "new style page breaking")))
    (item (text "Document updates run:")
      (enum (set-pretty-preference "document update times" answer)
            '("Once" "Twice" "Three times")
            (get-pretty-preference "document update times") 
            "10em"))
    (item (text "Fast environments:")
      (toggle (set-boolean-preference "fast environments" answer)
              (get-boolean-preference "fast environments")))))

(tm-widget (rendering-enunciations-preferences-widget)
  (aligned
    (item (text "Theorem background:")
      (input (set-preference "vault theorem color" answer) "string"
             (list (get-preference "vault theorem color")) "10em"))
    (item (text "Lemma background:")
      (input (set-preference "vault lemma color" answer) "string"
             (list (get-preference "vault lemma color")) "10em"))
    (item (text "Corollary background:")
      (input (set-preference "vault corollary color" answer) "string"
             (list (get-preference "vault corollary color")) "10em"))
    (item (text "Proposition background:")
      (input (set-preference "vault proposition color" answer) "string"
             (list (get-preference "vault proposition color")) "10em"))
    (item (text "Axiom background:")
      (input (set-preference "vault axiom color" answer) "string"
             (list (get-preference "vault axiom color")) "10em"))
    (item (text "Definition background:")
      (input (set-preference "vault definition color" answer) "string"
             (list (get-preference "vault definition color")) "10em"))
    (item (text "Notation background:")
      (input (set-preference "vault notation color" answer) "string"
             (list (get-preference "vault notation color")) "10em"))
    (item (text "Convention background:")
      (input (set-preference "vault convention color" answer) "string"
             (list (get-preference "vault convention color")) "10em"))
    (item (text "Conjecture background:")
      (input (set-preference "vault conjecture color" answer) "string"
             (list (get-preference "vault conjecture color")) "10em"))))

(tm-widget (rendering-remarks-preferences-widget)
  (aligned
    (item (text "Remark background:")
      (input (set-preference "vault remark color" answer) "string"
             (list (get-preference "vault remark color")) "10em"))
    (item (text "Note background:")
      (input (set-preference "vault note color" answer) "string"
             (list (get-preference "vault note color")) "10em"))
    (item (text "Example background:")
      (input (set-preference "vault example color" answer) "string"
             (list (get-preference "vault example color")) "10em"))
    (item (text "Warning background:")
      (input (set-preference "vault warning color" answer) "string"
             (list (get-preference "vault warning color")) "10em"))
    (item (text "Acknowledgments background:")
      (input (set-preference "vault acknowledgments color" answer) "string"
             (list (get-preference "vault acknowledgments color")) "10em"))))

(tm-widget (rendering-exercises-preferences-widget)
  (aligned
    (item (text "Exercise background:")
      (input (set-preference "vault exercise color" answer) "string"
             (list (get-preference "vault exercise color")) "10em"))
    (item (text "Problem background:")
      (input (set-preference "vault problem color" answer) "string"
             (list (get-preference "vault problem color")) "10em"))
    (item (text "Question background:")
      (input (set-preference "vault question color" answer) "string"
             (list (get-preference "vault question color")) "10em"))
    (item (text "Solution background:")
      (input (set-preference "vault solution color" answer) "string"
             (list (get-preference "vault solution color")) "10em"))
    (item (text "Answer background:")
      (input (set-preference "vault answer color" answer) "string"
             (list (get-preference "vault answer color")) "10em"))
    (item (text "Proof background:")
      (input (set-preference "vault proof color" answer) "string"
             (list (get-preference "vault proof color")) "10em"))))

(tm-widget (rendering-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "Document")
        (centered
          (dynamic (rendering-document-preferences-widget))))
      (tab (text "Enunciations")
        (centered
          (dynamic (rendering-enunciations-preferences-widget))))
      (tab (text "Remarks, Notes")
        (centered
          (dynamic (rendering-remarks-preferences-widget))))
      (tab (text "Exercises, Proofs")
        (centered
          (dynamic (rendering-exercises-preferences-widget))))))
  ===)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Keyboard preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-preference-names "text spacebar"
  ("default" "Default")
  ("allow multiple spaces" "Allow multiple spaces")
  ("glue multiple spaces" "Glue multiple spaces")
  ("no multiple spaces" "No multiple spaces"))

(define-preference-names "math spacebar"
  ("default" "Default")
  ("allow spurious spaces" "Allow spurious spaces")
  ("avoid spurious spaces" "Avoid spurious spaces")
  ("no spurious spaces" "No spurious spaces"))

(define-preference-names "automatic quotes"
  ("default" "Default")
  ("none" "Disabled")
  ("dutch" "Dutch")
  ("english" "English")
  ("french" "French")
  ("german" "German")
  ("spanish" "Spanish")
  ("swiss" "Swiss"))

(define-preference-names "automatic brackets"
  ("off" "Disabled")
  ("mathematics" "Inside mathematics" "mathematics")
  ("on" "Enabled"))

(define-preference-names "cyrillic input method"
  ("none" "None")
  ("translit" "Translit")
  ("jcuken" "Jcuken")
  ("yawerty" "Yawerty"))

(tm-widget (keyboard-input-preferences-widget)
  (vertical
    (aligned
      (item (text "Space bar in text mode:")
        (enum (set-pretty-preference "text spacebar" answer)
              '("Default" "No multiple spaces"
                "Glue multiple spaces" "Allow multiple spaces")
              (get-pretty-preference "text spacebar")
              "15em"))
      (item (text "Space bar in math mode:")
        (enum (set-pretty-preference "math spacebar" answer)
              '("Default" "No spurious spaces"
                "Avoid spurious spaces" "Allow spurious spaces")
              (get-pretty-preference "math spacebar")
              "15em"))
      (item (text "Automatic quotes:")
        (enum (set-pretty-preference "automatic quotes" answer)
              '("Default" "Disabled" "Dutch" "English" "French" "German" "Spanish" "Swiss")
              (get-pretty-preference "automatic quotes")
              "15em"))
      (item (text "Automatic brackets:")
        (enum (set-pretty-preference "automatic brackets" answer)
              '("Disabled" "Enabled" "Inside mathematics")
              (get-pretty-preference "automatic brackets")
              "15em"))
      (item (text "Cyrillic input method:")
        (enum (set-pretty-preference "cyrillic input method" answer)
              '("None" "Translit" "Jcuken" "Yawerty")
              (get-pretty-preference "cyrillic input method")
              "15em")))
    ===
    (hlist
      (text "Advanced settings:") //
      (explicit-buttons
        ("Edit keyboard shortcuts" (open-shortcuts-editor "" ""))))))

(tm-widget (keyboard-remote-preferences-widget)
  (hlist
    (aligned
      (item (text "Left:")
        (enum (set-preference "ir-left" answer) '("pageup" "")
              (get-preference "ir-left") "8em"))
      (item (text "Right:")
        (enum (set-preference "ir-right" answer) '("pagedown" "")
              (get-preference "ir-right") "8em"))
      (item (text "Up:")
        (enum (set-preference "ir-up" answer) '("home" "")
              (get-preference "ir-up") "8em"))
      (item (text "Down:")
        (enum (set-preference "ir-down" answer) '("end" "")
              (get-preference "ir-down") "8em")))
    ///
    (aligned
      (item (text "Center:")
        (enum (set-preference "ir-center" answer) '("return" "S-return" "")
              (get-preference "ir-center") "8em"))
      (item (text "Play:")
        (enum (set-preference "ir-play" answer) '("F5" "")
              (get-preference "ir-play") "8em"))
      (item (text "Pause:")
        (enum (set-preference "ir-pause" answer) '("escape" "")
              (get-preference "ir-pause") "8em"))
      (item (text "Menu:")
        (enum (set-preference "ir-menu" answer) '("." "")
              (get-preference "ir-menu") "8em")))))

(tm-widget (keyboard-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "Input")
        (centered
          (dynamic (keyboard-input-preferences-widget))))
      (tab (text "Remote Control")
        (centered
          (dynamic (keyboard-remote-preferences-widget))))))
  ===)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Mathematics preferences widget
;; FIXME: - "assuming" has no effect in refreshable widgets
;;        - Too much alignment tweaking      
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (math-keyboard-preferences-widget)
  (bold (text "Keyboard"))
  ======
  (aligned
    (meti (text "Use extensible brackets")
      (toggle (set-boolean-preference "use large brackets"
                                      answer)
              (get-boolean-preference "use large brackets")))))

(tm-widget (math-hints-preferences-widget)
  (bold (text "Contextual hints"))
  ======
  (refreshable "math-pref-context"
    (aligned
      (meti (text "Show full context")
        (toggle (set-boolean-preference "show full context" answer)
                (get-boolean-preference "show full context")))
      (meti (text "Show table cells")
        (toggle (set-boolean-preference "show table cells" answer)
                (get-boolean-preference "show table cells")))
      (meti (text "Show current focus")
        (toggle (set-boolean-preference "show focus" answer)
                (get-boolean-preference "show focus")))
      (assuming (get-boolean-preference "semantic editing")
        (meti (text "Only show semantic focus")
          (toggle (set-boolean-preference
                   "show only semantic focus" answer)
                  (get-boolean-preference
                   "show only semantic focus")))))))

(tm-widget (math-semantics-preferences-widget)
  (bold (text "Semantics"))
  ======
  (refreshable "math-pref-semantic-selections"
    (aligned
      (meti (text "Semantic editing")
        (toggle (and (set-boolean-preference "semantic editing"
                                             answer)
                     (refresh-now "math-pref-semantic-selections")
                     (refresh-now "math-pref-context"))
                (get-boolean-preference "semantic editing")))
      (assuming (get-boolean-preference "semantic editing")
        (meti (text "Semantic selections")
          (toggle (set-boolean-preference "semantic selections"
                                          answer)
                  (get-boolean-preference
                   "semantic selections"))))
      (assuming #f
        (meti (text "Semantic correctness")
          (toggle (set-boolean-preference "semantic correctness"
                                          answer)
                  (get-boolean-preference
                   "semantic correctness")))))))

(tm-widget (math-correction-preferences-widget)
  (bold (text "Correction"))
  ======
  (aligned
    (meti (text "Remove superfluous invisible operators")
      (toggle (set-boolean-preference
               "manual remove superfluous invisible" answer)
              (get-boolean-preference
               "manual remove superfluous invisible")))
    (meti (text "Insert missing invisible operators")
      (toggle (set-boolean-preference
               "manual insert missing invisible" answer)
              (get-boolean-preference
               "manual insert missing invisible")))
    (meti (text "Homoglyph substitutions")
      (toggle (set-boolean-preference
               "manual homoglyph correct" answer)
              (get-boolean-preference
               "manual homoglyph correct")))))

(tm-widget (editing-math-preferences-widget)
  (padded
    (hlist
      (vlist
        (dynamic (math-keyboard-preferences-widget))
        ====== ======
        (dynamic (math-hints-preferences-widget))
        (glue #f #t 0 1))
      (glue #f #f 30 0)
      (vlist
        (dynamic (math-semantics-preferences-widget))
        ====== ======
        (dynamic (math-correction-preferences-widget))
        (glue #f #t 0 1)))))

(tm-widget (editing-programming-preferences-widget)
  (aligned
    (item (text "Scripting language:")
      (enum (set-pretty-preference "scripting language" answer)
            (scripts-preferences-list)
            (get-pretty-preference "scripting language")
            "15em"))
    (item (text "Highlight matching brackets:")
      (toggle (set-boolean-preference "prog:highlight brackets" answer)
              (get-boolean-preference "prog:highlight brackets")))
    (item (text "Automatic program brackets:")
      (toggle (set-boolean-preference "prog:automatic brackets" answer)
              (get-boolean-preference "prog:automatic brackets")))
    (item (text "Use smart bracket selections:")
      (toggle (set-boolean-preference "prog:select brackets" answer)
              (get-boolean-preference "prog:select brackets")))))

(define-preferences
  ("latex->texmacs:matrix-recognition" "on" noop)
  ("latex->texmacs:aligned-to-eqnarray" "on" noop)
  ("latex->texmacs:align-to-aligned" "on" noop)
  ("latex->texmacs:operator-d-is-differential" "on" noop)
  ("latex->texmacs:roman-d-is-differential" "on" noop)
  ("latex->texmacs:text-d-is-differential" "on" noop)
  ("latex->texmacs:parse-bbbk" "on" noop))

(tm-widget (editing-importer-preferences-widget)
  (aligned
    (item (text "Recognize matrices and determinants disguised as arrays:")
      (toggle (set-boolean-preference "latex->texmacs:matrix-recognition" answer)
              (get-boolean-preference "latex->texmacs:matrix-recognition")))
    (item (text "Treat 'align' as 'aligned':")
      (toggle (set-boolean-preference "latex->texmacs:align-to-aligned" answer)
              (get-boolean-preference "latex->texmacs:align-to-aligned")))
    (item (text "Convert 'aligned' blocks into 'eqnarray' environments:")
      (toggle (set-boolean-preference "latex->texmacs:aligned-to-eqnarray" answer)
              (get-boolean-preference "latex->texmacs:aligned-to-eqnarray")))
    (item (text "Parse operator d as differential d:")
      (toggle (set-boolean-preference "latex->texmacs:operator-d-is-differential" answer)
              (get-boolean-preference "latex->texmacs:operator-d-is-differential")))
    (item (text "Parse Roman d as differential d:")
      (toggle (set-boolean-preference "latex->texmacs:roman-d-is-differential" answer)
              (get-boolean-preference "latex->texmacs:roman-d-is-differential")))
    (item (text "Parse text d as differential d:")
      (toggle (set-boolean-preference "latex->texmacs:text-d-is-differential" answer)
              (get-boolean-preference "latex->texmacs:text-d-is-differential")))
    (item (text "Parse blackboard k as Bbbk:")
      (toggle (set-boolean-preference "latex->texmacs:parse-bbbk" answer)
              (get-boolean-preference "latex->texmacs:parse-bbbk")))))

(tm-widget (editing-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "Maths")
        (centered
          (dynamic (editing-math-preferences-widget))))
      (tab (text "Programming")
        (centered
          (dynamic (editing-programming-preferences-widget))))
      (tab (text "Formula Importer")
        (centered
          (dynamic (editing-importer-preferences-widget))))))
  ===)

(tm-widget (math-preferences-widget*)
  (dynamic (math-keyboard-preferences-widget))
  ====== ======
  (dynamic (math-hints-preferences-widget))
  ====== ======
  (dynamic (math-semantics-preferences-widget))
  ====== ======
  (dynamic (math-correction-preferences-widget)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Conversion preferences widget
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Html ----------

(define (export-formulas-as-mathjax on?)
  (set-boolean-preference "texmacs->html:mathjax" on?)
  (when on?
    (set-boolean-preference "texmacs->html:mathml" #f)
    (set-boolean-preference "texmacs->html:images" #f)
    (refresh-now "texmacs to html")))

(define (export-formulas-as-mathml on?)
  (set-boolean-preference "texmacs->html:mathml" on?)
  (when on?
    (set-boolean-preference "texmacs->html:mathjax" #f)
    (set-boolean-preference "texmacs->html:images" #f)
    (refresh-now "texmacs to html")))

(define (export-formulas-as-images on?)
  (set-boolean-preference "texmacs->html:images" on?)
  (when on?
    (set-boolean-preference "texmacs->html:mathjax" #f)
    (set-boolean-preference "texmacs->html:mathml" #f)
    (refresh-now "texmacs to html")))

(tm-widget (html-preferences-widget)
  ======
  (bold (text "TeXmacs -> Html"))
  ===
  (refreshable "texmacs to html"
    (aligned
      (meti (hlist // (text "Use CSS for more advanced formatting"))
        (toggle (set-boolean-preference "texmacs->html:css" answer)
                (get-boolean-preference "texmacs->html:css")))
      (meti (hlist // (text "Export mathematical formulas as MathJax"))
        (toggle (export-formulas-as-mathjax answer)
                (get-boolean-preference "texmacs->html:mathjax")))
      (meti (hlist // (text "Export mathematical formulas as MathML"))
        (toggle (export-formulas-as-mathml answer)
                (get-boolean-preference "texmacs->html:mathml")))
      (meti (hlist // (text "Export mathematical formulas as images"))
        (toggle (export-formulas-as-images answer)
                (get-boolean-preference "texmacs->html:images"))))
    ===
    (hlist
      (text "CSS stylesheet:") //
      (enum (set-preference "texmacs->html:css-stylesheet" answer)
            '("---"
              "https://www.texmacs.org/css/web-article.css"
              "https://www.texmacs.org/css/web-article-dark.css"
              "https://www.texmacs.org/css/web-article-colored.css"
              "https://www.texmacs.org/css/web-article-dark-colored.css"
              "")
            (get-preference "texmacs->html:css-stylesheet") "18em")))
  ====== ======
  (bold (text "Html -> TeXmacs"))
  ===
  (refreshable "html -> texmacs"
    (aligned
      (meti (hlist // (text "Try to import formulas using LaTeX annotations"))
        (toggle (set-boolean-preference
                 "mathml->texmacs:latex-annotations" answer)
                (get-boolean-preference
                 "mathml->texmacs:latex-annotations"))))))

;; LaTeX ----------

(define-preference-names "texmacs->latex:encoding"
  ("ascii" "Ascii")
  ("cork"  "Cork with catcodes")
  ("utf-8" "Utf-8 with inputenc"))

(define (get-latex-source-tracking)
  (or (get-boolean-preference "latex->texmacs:source-tracking")
      (get-boolean-preference "texmacs->latex:source-tracking")))

(define (set-latex-source-tracking on?)
  (set-boolean-preference "latex->texmacs:source-tracking" on?)
  (set-boolean-preference "texmacs->latex:source-tracking" on?)
  (refresh-now "source-tracking"))

(define (get-latex-conservative)
  (and (get-boolean-preference "latex->texmacs:conservative")
       (get-boolean-preference "texmacs->latex:conservative")))

(define (set-latex-conservative on?)
  (set-boolean-preference "latex->texmacs:conservative" on?)
  (set-boolean-preference "texmacs->latex:conservative" on?)
  (refresh-now "source-tracking"))

(define (get-latex-transparent-source-tracking)
  (or (get-boolean-preference "latex->texmacs:transparent-source-tracking")
      (get-boolean-preference "texmacs->latex:transparent-source-tracking")))

(define (set-latex-transparent-source-tracking on?)
  (set-boolean-preference "latex->texmacs:transparent-source-tracking" on?)
  (set-boolean-preference "texmacs->latex:transparent-source-tracking" on?))

(tm-widget (latex-preferences-widget)
  ======
  (bold (text "LaTeX -> TeXmacs"))
  ===
  (aligned
    (meti (hlist // (text "Import sophisticated objects as pictures"))
      (toggle
        (set-boolean-preference "latex->texmacs:fallback-on-pictures" answer)
        (get-boolean-preference "latex->texmacs:fallback-on-pictures"))))
  ====== ======
  (bold (text "TeXmacs -> LaTeX"))
  ===
  (aligned
    (meti (hlist // (text "Replace TeXmacs styles with no LaTeX equivalents"))
      (toggle (set-boolean-preference "texmacs->latex:replace-style" answer)
              (get-boolean-preference "texmacs->latex:replace-style")))
    (meti (hlist // (text "Expand TeXmacs macros with no LaTeX equivalents"))
      (toggle (set-boolean-preference "texmacs->latex:expand-macros" answer)
              (get-boolean-preference "texmacs->latex:expand-macros")))
    (meti (hlist // (text "Expand user-defined macros"))
      (toggle (set-boolean-preference "texmacs->latex:expand-user-macros" answer)
              (get-boolean-preference "texmacs->latex:expand-user-macros")))
    (meti (hlist // (text "Export bibliographies as links"))
      (toggle (set-boolean-preference "texmacs->latex:indirect-bib" answer)
              (get-boolean-preference "texmacs->latex:indirect-bib")))
    (meti (hlist // (text "Allow for macro definitions in preamble"))
      (toggle (set-boolean-preference "texmacs->latex:use-macros" answer)
              (get-boolean-preference "texmacs->latex:use-macros"))))
  ===
  (aligned
    (item (text "Character encoding:")
      (enum (set-pretty-preference "texmacs->latex:encoding" answer)
            '("Ascii" "Cork with catcodes" "Utf-8 with inputenc")
            (get-pretty-preference "texmacs->latex:encoding")
            "15em")))
  ====== ======
  (bold (text "Conservative conversion options"))
  ===
  (refreshable "source-tracking"
    (aligned
      (meti (hlist // (text "Keep track of source code"))
        (toggle
         (set-latex-source-tracking answer)
         (get-latex-source-tracking)))
      (meti (hlist // (text "Only convert changes with respect to tracked version"))
        (toggle
         (set-latex-conservative answer)
         (get-latex-conservative)))
      (meti (when (get-latex-source-tracking)
              (hlist // (text "Guarantee transparent source tracking")))
        (when (get-latex-source-tracking)
          (toggle
           (set-latex-transparent-source-tracking answer)
           (get-latex-transparent-source-tracking))))
      (meti (when (get-latex-source-tracking)
              (hlist // (text "Store tracking information in LaTeX files")))
        (when (get-latex-source-tracking)
          (toggle
           (set-boolean-preference "texmacs->latex:attach-tracking-info" answer)
           (get-boolean-preference "texmacs->latex:attach-tracking-info")))))))

;; BibTeX ----------

(define (get-bibtm-conservative)
  (get-boolean-preference "bibtex->texmacs:conservative"))

(define (set-bibtm-conservative on?)
  (set-boolean-preference "bibtex->texmacs:conservative" on?))

(define (get-tmbib-conservative)
  (get-boolean-preference "texmacs->bibtex:conservative"))

(define (set-tmbib-conservative on?)
  (set-boolean-preference "texmacs->bibtex:conservative" on?))

(tm-widget (bibtex-preferences-widget)
  ===
  (bold (text "BibTeX -> TeXmacs"))
  ===
  (aligned
    (item (text "BibTeX command:")
      (enum (set-pretty-preference "bibtex command" answer)
            '("bibtex" "biber" "biblatex" "rubibtex" "")
            (get-pretty-preference "bibtex command")
            "15em")))
  ===
  (aligned
    (meti (hlist // (text "Only convert changes when re-importing"))
      (toggle (set-bibtm-conservative answer)
              (get-bibtm-conservative))))
  ====== ======
  (bold (text "TeXmacs -> BibTeX"))
  ===
  (aligned
    (meti (hlist // (text "Only convert changes with respect to imported version"))
      (toggle (set-tmbib-conservative answer)
              (get-tmbib-conservative)))))

;; Verbatim ----------

(define-preference-names "texmacs->verbatim:encoding"
  ("auto" "Automatic")
  ("cork" "Cork")
  ("iso-8859-1" "Iso-8859-1")
  ("iso-8859-2" "Iso-8859-2")
  ("utf-8" "Utf-8"))

(define-preference-names "verbatim->texmacs:encoding"
  ("auto" "Automatic")
  ("cork" "Cork")
  ("iso-8859-1" "Iso-8859-1")
  ("iso-8859-2" "Iso-8859-2")
  ("utf-8" "Utf-8"))

(tm-widget (verbatim-preferences-widget)
  ======
  (bold (text "TeXmacs -> Verbatim"))
  ===
  (aligned
    (meti (hlist // (text "Use line wrapping for lines which are longer than 80 characters"))
      (toggle (set-boolean-preference "texmacs->verbatim:wrap" answer)
              (get-boolean-preference "texmacs->verbatim:wrap"))))
  ===
  (aligned
    (item (text "Character encoding:")
      (enum (set-pretty-preference "texmacs->verbatim:encoding" answer)
            '("Automatic" "Cork" "Iso-8859-1" "Iso-8859-2" "Utf-8")
            (get-pretty-preference "texmacs->verbatim:encoding")
            "12em")))
  ====== ======
  (bold (text "Verbatim -> TeXmacs"))
  ===
  (aligned
    (meti (hlist // (text "Merge lines into paragraphs unless separated by blank lines"))
      (toggle (set-boolean-preference "verbatim->texmacs:wrap" answer)
              (get-boolean-preference "verbatim->texmacs:wrap"))))
  ===
  (aligned
    (item (text "Character encoding:")
      (enum (set-pretty-preference "verbatim->texmacs:encoding" answer)
            '("Automatic" "Cork" "Iso-8859-1" "Iso-8859-2" "Utf-8")
            (get-pretty-preference "verbatim->texmacs:encoding")
            "12em"))))

;; Pdf ----------
(define-preference-names "texmacs->pdf:version"
  ("Default" "default")
  ("1.4" "1.4")
  ("1.5" "1.5")
  ("1.6" "1.6")
  ("1.7" "1.7"))

(tm-widget (pdf-preferences-widget)
  ======
  (bold (text "TeXmacs -> Pdf/Postscript"))
  ===
  (aligned
    (assuming (supports-native-pdf?)
      (meti (hlist // (text "Produce Pdf using native export filter"))
        (toggle (set-boolean-preference "native pdf" answer)
                (get-boolean-preference "native pdf"))))
    (assuming (supports-ghostscript?)
      (meti (hlist // (text "Produce Postscript using native export filter"))
        (toggle (set-boolean-preference "native postscript" answer)
                (get-boolean-preference "native postscript"))))
    (meti (hlist // (text "Expand beamer slides"))
      (toggle (set-boolean-preference "texmacs->pdf:expand slides" answer)
              (get-boolean-preference "texmacs->pdf:expand slides")))
    (assuming (supports-native-pdf?)
      (meti (hlist // (text "Distill encapsulated Pdf files"))
        (toggle (set-boolean-preference "texmacs->pdf:distill inclusion" answer)
                (get-boolean-preference "texmacs->pdf:distill inclusion")))
      (meti (hlist // (text "Check exported Pdf files for correctness"))
        (toggle (set-boolean-preference "texmacs->pdf:check" answer)
                (get-boolean-preference "texmacs->pdf:check")))))
  (assuming (supports-native-pdf?)
    (aligned
      (item (text "Pdf version number:")
        (enum (set-preference "texmacs->pdf:version" answer)
              '("default" "1.4" "1.5" "1.6" "1.7")
              (get-preference "texmacs->pdf:version") "12em")))))

;; Images ----------

(define (pretty-format-list)
  (let* ((desired-image-format-list '(("svg" "Svg")  ("eps" "Eps")
           ("png" "Png")("tif" "Tiff") ("jpg" "Jpeg") ("pdf" "Pdf")))
         (valid-image-format-list 
           (filter (lambda (x) (file-converter-exists? "x.pdf" (string-append "x." (car x))))
             desired-image-format-list)))
   (eval `(define-preference-names "texmacs->image:format" ,@valid-image-format-list))
   (cadr (apply map list valid-image-format-list))))

(define (supports-inkscape?) (url-exists-in-path? "inkscape"))

(tm-widget (image-preferences-widget)
  ======
  (bold (text "TeXmacs -> Image"))
  ===
  (aligned
    (item (text "Bitmap export resolution (dpi):")
      (enum (set-preference "texmacs->image:raster-resolution" answer)
            '("1200" "600" "300" "150" "")
            (get-preference "texmacs->image:raster-resolution")
            "8em"))
    (item (text "Clipboard image format:")
      (enum (set-pretty-preference "texmacs->image:format" answer)
            (pretty-format-list)
            (get-pretty-preference "texmacs->image:format")
            "8em")))
  ====== ======
  (bold (text "Image -> TeXmacs"))
  ===
  (aligned
    (meti
      (when (supports-inkscape?)
        (hlist // (text "Use Inkscape for conversion from SVG")))
      (when (supports-inkscape?)
        (toggle (set-boolean-preference
                 "image->texmacs:svg-prefer-inkscape" answer)
                (get-boolean-preference
                 "image->texmacs:svg-prefer-inkscape"))))))

;; All converters ----------

(tm-widget (conversion-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "Html")
        (centered
          (dynamic (html-preferences-widget))))
      (tab (text "LaTeX")
        (centered
          (dynamic (latex-preferences-widget))))
      (tab (text "BibTeX")
        (centered
          (dynamic (bibtex-preferences-widget))))
      (tab (text "Verbatim")
        (centered
          (dynamic (verbatim-preferences-widget))))
      (assuming (or (supports-native-pdf?) (supports-ghostscript?))
        (tab (text "Pdf")
          (centered
            (dynamic (pdf-preferences-widget)))))
      (tab (text "Image")
        (centered
          (dynamic (image-preferences-widget))))))
  ===)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Other
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-preference-names "autosave"
  ("5" "5 sec")
  ("30" "30 sec")
  ("120" "120 sec")
  ("300" "300 sec")
  ("0" "Disable"))

(define-preference-names "security"
  ("accept no scripts" "Accept no scripts")
  ("prompt on scripts" "Prompt on scripts")
  ("accept all scripts" "Accept all scripts"))

(define-preference-names "updater:interval"
  ("0" "Never")
  ("0" "Unsupported")
  ("24" "Once a day")
  ("168" "Once a week")
  ("720" "Once a month"))

(define-preference-names "document update times"
  ("1" "Once")
  ("2" "Twice")
  ("3" "Three times"))

(define-preference-names "scripting language"
  ("none" "None"))

(define (updater-last-check-formatted)
  "Time since last update check formatted for use in the preferences dialog"
  (with c (updater-last-check)
    (if (<= c 0) 
        "Never"
        (with h (ceiling (/ (- (current-time) c) 3600))
          (cond ((< h 24) (replace "Less than %1 hour(s) ago" h))
                ((< h 720) (replace "%1 days ago" (ceiling (/ h 24))))
                (else (translate "More than 1 month ago")))))))

(define (last-check-string)
  (if (updater-supported?)
      (updater-last-check-formatted)
      "Never (unsupported)"))

(define (automatic-checks-choices)
  (if (updater-supported?)
      '("Never" "Once a day" "Once a week" "Once a month")
      '("Unsupported")))

(tm-define (scripts-preferences-list)
  (lazy-plugin-force)
  (with l (scripts-list)
    (for (x l) (set-preference-name "scripting language" x (scripts-name x)))
    (cons "None" (map scripts-name l))))

(tm-widget (script-preferences-widget)
  (aligned
    (item (text "Execution of scripts:")
      (enum (set-pretty-preference "security" answer)
            '("Accept no scripts" "Prompt on scripts" "Accept all scripts")
            (get-pretty-preference "security")
            "15em"))))

(tm-widget (security-preferences-widget)
  (refreshable "security-preferences-refresher"
    (padded
      ======
      (bold (text "Wallet"))
      ===
      (dynamic (wallet-preferences-widget))
      ======
      (bold (text "GnuPG Configuration"))
      ===
      (dynamic (gpg-preferences-widget))
      ;;====== ======
      ;;(bold (text "Scripts")) 
      ;;===
      ;;(dynamic (script-preferences-widget))
      )))

(tm-widget (security-misc-preferences-widget)
  (aligned
    (item (text "Script execution:")
      (enum (set-pretty-preference "security" answer)
            '("Accept no scripts" "Prompt on scripts" "Accept all scripts")
            (get-pretty-preference "security")
            "15em"))
    (item (text "Encryption:")
      (toggle (set-boolean-preference "experimental encryption" answer)
              (get-boolean-preference "experimental encryption")))))

(tm-widget (ai-preferences-widget)
  (vertical
    (aligned
      (item (text "AI engine:")
        (enum (set-preference "ai" answer)
              '("off" "chatgpt" "gemini" "llama" "open-mistral-7b")
              (get-preference "ai")
              "12em"))
      (item (text "OpenAI API key:")
        (input (set-preference "openai api key" answer) "string"
               (list (get-preference "openai api key"))
               "20em"))
      (item (text "Gemini API key:")
        (input (set-preference "gemini api key" answer) "string"
               (list (get-preference "gemini api key"))
               "20em"))
      (item (text "Mistral API key:")
        (input (set-preference "mistral api key" answer) "string"
               (list (get-preference "mistral api key"))
               "20em")))))

(tm-widget (other-preferences-widget)
  ===
  (padded
    (tabs
      (tab (text "AI")
        (centered
          (dynamic (ai-preferences-widget))))
      (tab (text "Security")
        (centered
          (dynamic (security-misc-preferences-widget))
          ======
          (bold (text "Advanced Security"))
          ======
          (dynamic (security-preferences-widget)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Plugin preferences widget
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define prefs-plugin-table (make-ahash-table))

(tm-define (prefs-plugin-get)
  (or (ahash-ref prefs-plugin-table :current) "scheme"))

(tm-define (prefs-plugin-set name)
  (ahash-set! prefs-plugin-table :current name)
  (refresh-now "plugin-prefs")
  (update-menus))

(tm-widget (plugin-preferences-list)
  (scrollable
    (choice (prefs-plugin-set (name->plugin answer))
            (map plugin->name (plugins-with-preferences))
            (plugin->name (prefs-plugin-get)))))

(tm-widget (plugin-preferences-widget*)
  (centered
    (dynamic (plugin-preferences-widget (prefs-plugin-get)))))

(tm-widget (plugins-preferences-widget)
  (padded
    (horizontal
      (vertical
        (resize "150px" "300px"
          (dynamic (plugin-preferences-list)))
        (glue #f #t 0 0))
      ///
      (vertical
        (refreshable "plugin-prefs"
          (promise (menu-dynamic
                     (dynamic (plugin-preferences-widget (prefs-plugin-get))))))
        (glue #f #t 400 0)))))

(tm-tool* (plugin-preferences-tool win)
  (:name (string-append (plugin->name (prefs-plugin-get)) " preferences"))
  (centered
    (dynamic (plugin-preferences-widget (prefs-plugin-get)))))

(tm-widget (plugin-titled-preferences-widget name)
  (division "title"
    (text (string-append (plugin->name name) " preferences")) >>)
  (padded
    (dynamic (plugin-preferences-widget name))))

(tm-tool* (plugins-preferences-tool win)
  (:name "Plugin preferences")
  (centered
    (resize "250px" "200px"
      (dynamic (plugin-preferences-list))))
  === ===
  (refreshable "plugin-prefs"
    (dynamic (plugin-titled-preferences-widget (prefs-plugin-get)))))

(tm-define (open-plugin-preferences name)
  (:interactive #t)
  (prefs-plugin-set name)
  (if (side-tools?)
      (tool-select :right 'plugin-preferences-tool)
      (top-window plugin-preferences-widget*
                  (string-append (plugin->name name) " preferences"))))

(tm-define (open-plugins-preferences)
  (:interactive #t)
  (and-with l (plugins-with-preferences)
    (prefs-plugin-set (car l)))
  (if (side-tools?)
      (tool-select :right 'plugins-preferences-tool)
      (top-window plugins-preferences-widget "Plugin preferences")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Preferences widget
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (preferences-widget)
  (centered
    (icon-tabs
      (icon-tab "tm_prefs_general.xpm" (text "General")
        (centered
          (dynamic (general-preferences-widget))))
      (icon-tab "tm_prefs_keyboard.xpm" (text "Keyboard")
        (centered
          (dynamic (keyboard-preferences-widget))))
      ;; TODO: please implement nice icon tabs first before
      ;; adding new tabs in the preferences widget
      ;; The tabs currently take too much horizontal space
      (icon-tab "tm_math_preferences.xpm" (text "Editing")
        (centered
          (dynamic (editing-preferences-widget))))
      (icon-tab "tm_view.svg" (text "Rendering")
        (dynamic (rendering-preferences-widget)))
      (icon-tab "tm_prefs_convert.xpm" (text "Convert")
        (dynamic (conversion-preferences-widget)))
      (icon-tab "tm_link.svg" (text "Vault")
        (centered
          (dynamic (vault-preferences-widget))))
      (icon-tab "tm_prefs_other.xpm" (text "Other")
        (centered
          (dynamic (other-preferences-widget)))))))

(define preferences-window-count 0)

(tm-define (preferences-open?)
  (> preferences-window-count 0))

(tm-define (open-preferences-window)
  (:interactive #t)
  (when (not (preferences-open?))
    (set! preferences-window-count (+ preferences-window-count 1))
    (top-window preferences-widget "User preferences"
                (lambda () (set! preferences-window-count (- preferences-window-count 1))))))

(tm-define (open-preferences)
  (:interactive #t)
  (if (side-tools?)
      (tool-select :right 'preferences-tool)
      (open-preferences-window)))
