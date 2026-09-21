
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : generic-menu.scm
;; DESCRIPTION : default focus menu
;; COPYRIGHT   : (C) 2010  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic generic-menu)
  (:use (utils edit variants)
        (utils edit selections)
        (generic generic-edit)
        (generic format-drd)
        (generic document-edit)
        (source source-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Variants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (focus-variant-menu t)
  (push-focus t
    (for (v (focus-variants-of t))
      ((eval (focus-tag-name v))
       (pull-focus t
         (variant-set-keep-numbering t v))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Subroutines for hidden fields
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (string-input-name t i)
  (let* ((name (tree-child-name* t i))
         (s (string-append (upcase-first name) ":")))
    (assuming (== name "")
      //)
    (assuming (!= name "")
      (glue #f #f 3 0)
      (mini #t (group (eval s))))))    

(tm-menu (string-input-icon t i)
  (push-focus t
    (let* ((name (tree-child-name* t i))
           (type (tree-child-type t i))
           (s (string-append (upcase-first name) ":"))
           (active? (inputter-active? (tree-ref t i) type))
           (props (child-proposals t i))
           (in (if active? (inputter-decode (tree-ref t i) type) "n.a."))
           (fm (type->format type))
           (w (type->width type))
           (setter (lambda (x)
                     (pull-focus t
                       (when x
                         (tree-set t i (inputter-encode x type))
                         (focus-tree-modified t))))))
      (dynamic (string-input-name t i))
      (assuming props
        (mini #t
          (=> (eval in)
              (for (prop props)
                (assuming (string? prop)
                  ((eval prop) (setter prop)))
                (assuming (== prop :other)
                  ---
                  ("Other"
                   (interactive setter (list (upcase-first name) fm in))))))))
      (assuming (not props)
        (when active?
          (mini #t
            (input (setter answer) fm (list in) w)))))))

(tm-menu (string-input-icon t i)
  (:require (== (tree-child-type t i) "color"))
  (push-focus t
    (let* ((name (tree-child-name* t i))
           (s (string-append (upcase-first name) ":"))
           (active? (inputter-active? (tree-ref t i) "color"))
           (in (if active? (inputter-decode (tree-ref t i) "color") ""))
           (setter (lambda (x)
                     (pull-focus t
                       (when x
                         (tree-set t i (inputter-encode x "color"))
                         (focus-tree-modified t))))))
      (dynamic (string-input-name t i))
      (=> (color (tree->stree (tree-ref t i)) #f #f 24 16)
          (pick-background "" (setter answer))
          ---
          ("Palette" (interactive-color setter '()))
          ("Pattern" (open-pattern-selector setter "1cm"))
          ("Gradient" (open-gradient-selector setter))
          ("Picture" (open-background-picture-selector setter))
          ("Other" (interactive setter
                     (list (upcase-first name) "color" in)))))))

(tm-define (child-proposals t i)
  (:require (== (tree-child-type t i) "duration"))
  (list "0.25s" "0.5s" "1s" "1.5s" "2s" "2.5s" "3s" "4s" "5s" "10s" :other))

(tm-menu (string-input-menu t i)
  (push-focus t
    (let* ((name (tree-child-long-name* t i))
           (s `(concat "Set " ,name))
           (prompt (upcase-first name))
           (type (tree-child-type t i))
           (fm (type->format type))
           (active? (inputter-active? (tree-ref t i) type))
           (props (child-proposals t i))
           (in (if active? (inputter-decode (tree-ref t i) type) "n.a."))
           (setter (lambda (x)
                     (pull-focus t
                       (when x
                         (tree-set t i (inputter-encode x type))
                         (focus-tree-modified t))))))
      (assuming (!= name "")
        (assuming props
          (-> (eval s)
              (for (prop props)
                (assuming (string? prop)
                  ((eval prop) (setter prop)))
                (assuming (== prop :other)
                  ---
                  ("Other"
                   (interactive setter (list (upcase-first name) fm in)))))))
        (assuming (not props)
          (when (inputter-active? (tree-ref t i) type)
            ((eval s)
             (interactive setter
               (list prompt fm (inputter-decode (tree-ref t i) type))))))))))

(tm-menu (string-input-icon t i)
  (:require (string-variable-name? t i))
  (with c (tree-ref t i)
    (with s (if (tree-atomic? c) (tree->string c) "n.a.")
      (glue #f #f 3 0)
      (mini #t (group (eval (string-append s ":")))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Unified accessors for local and global parameters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (parameter-interactive-set l mode)
  (:interactive #t)
  (interactive (lambda (s) (parameter-set l s mode))
    (list (or (logic-ref env-var-description% l) l) "string"
          (parameter-get l mode))))

(tm-define (parameter-toggle l mode)
  (:check-mark "*" parameter-enabled?)
  (with new (if (== (parameter-get l mode) "true") "false" "true")
    (parameter-reset l mode)
    (delayed
      (when (!= new (parameter-get l mode))
        (parameter-set l new mode)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Submenus for editing various types of style parameters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (parameter-choice-menu l cs mode)
  (with ss (list-filter cs parameter-value?)
    ((check "Default" "*" (parameter-default? l mode))
     (parameter-reset l mode))
    (if (nnull? ss)
        ---
        (for (c ss)
          (assuming (string? c)
            ((check (eval (upcase-first c)) "*" (parameter-test? l c mode))
             (parameter-set l c mode)))
          (assuming (and (list-2? c) (string? (car c)))
            ((check (eval (car c)) "*"
                    (parameter-test? l (cadr c) mode))
             (parameter-set l (cadr c) mode)))))
    (if (and (nnull? ss) (in? :other cs))
        ---)
    (if (in? :other cs)
        ("Other" (parameter-interactive-set l mode)))))

(tm-menu (parameter-submenu l mode)
  (dynamic (parameter-choice-menu l (list :other) mode)))

(tm-menu (parameter-submenu l mode)
  (:require (== (tree-label-type (string->symbol l)) "color"))
  (with setter (lambda (col) (delayed (:idle 250) (parameter-set l col mode)))
    ((check "Default" "*" (parameter-default? l mode))
     (parameter-reset l mode))
    ---
    (pick-background "" (setter answer))
    ---
    (if (in? l (list "locus-color" "visited-color"
                     "radioactive-link-color"))
        ((check "Preserve" "*" (parameter-test? l "preserve" mode))
         (parameter-set l "preserve" mode)))
    ("Palette" (interactive-color setter '()))
    ("Pattern" (open-pattern-selector setter "1cm"))
    ("Gradient" (open-gradient-selector setter))
    ("Picture" (open-background-picture-selector setter))
    ("Other" (parameter-interactive-set l mode))))

(tm-menu (parameter-submenu l mode)
  ;;(:require (== (tree-label-type (string->symbol l)) "font"))
  (:require (string-ends? l "-font"))
  ((check "Default" "*" (parameter-default? l mode))
   (parameter-reset l mode))
  ---
  ((check "Roman" "*" (parameter-test? l "roman" mode))
   (parameter-set l "roman" mode))
  ((check "Stix" "*" (parameter-test? l "stix" mode))
   (parameter-set l "stix" mode))
  ((check "Bonum" "*" (parameter-test? l "bonum" mode))
   (parameter-set l "bonum" mode))
  ((check "Pagella" "*" (parameter-test? l "pagella" mode))
   (parameter-set l "pagella" mode))
  ((check "Schola" "*" (parameter-test? l "schola" mode))
   (parameter-set l "schola" mode))
  ((check "Termes" "*" (parameter-test? l "termes" mode))
   (parameter-set l "termes" mode))
  ---
  (with prefix (string-drop-right l 4)
    ("Other" (open-document-other-font-selector prefix))))

(tm-menu (parameter-submenu l mode)
  (:require (== (tree-label-type (string->symbol l)) "font-size"))
  ((check "Default" "*" (parameter-default? l mode))
   (parameter-reset l mode))
  ---
  ((check "Small" "*" (parameter-test? l "0.841" mode))
   (parameter-set l "0.841" mode))
  ((check "Normal" "*" (parameter-test? l "1" mode))
   (parameter-set l "1" mode))
  ((check "Large" "*" (parameter-test? l "1.189" mode))
   (parameter-set l "1.189" mode))
  ((check "Very large" "*" (parameter-test? l "1.414" mode))
   (parameter-set l "1.414" mode))
  ((check "Huge" "*" (parameter-test? l "1.682" mode))
   (parameter-set l "1.682" mode))
  ---
  ("Other" (parameter-interactive-set l mode)))

(tm-menu (parameter-submenu l mode)
  (:require (parameter-choice-list l))
  (with cs (parameter-choice-list l)
    (dynamic (parameter-choice-menu l cs mode))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Editing style parameters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (focus-parameter-menu-item l mode)
  (:require (and (tree-label-parameter? (string->symbol l))
                 (string? (parameter-get l mode))
                 (nin? (tree-label-type (string->symbol l))
                       (list "unknown" "regular" "adhoc"))))
  (-> (eval (focus-tag-name (string->symbol l)))
      (dynamic (parameter-choice-menu l (list :other) mode))))

(tm-menu (focus-parameter-menu-item l mode)
  (:require (and (tree-label-parameter? (string->symbol l))
                 (string? (parameter-get l mode))
                 (== (tree-label-type (string->symbol l)) "boolean")))
  ((check (eval (focus-tag-name (string->symbol l))) "v"
          (== (parameter-get l mode) "true"))
   (parameter-toggle l mode)))

(tm-menu (focus-parameter-menu-item l mode)
  (:require (and (tree-label-parameter? (string->symbol l))
                 (or (== (tree-label-type (string->symbol l)) "color")
                     ;;(== (tree-label-type (string->symbol l)) "font")
                     (string-ends? l "-font")
                     (== (tree-label-type (string->symbol l)) "font-size"))))
  (-> (eval (focus-tag-name (string->symbol l)))
      (dynamic (parameter-submenu l mode))))

(tm-menu (focus-parameter-menu-item l mode)
  (:require (parameter-choice-list l))
  (-> (eval (focus-tag-name (string->symbol l)))
      (dynamic (parameter-submenu l mode))))

(tm-define (parameter-show-in-menu? l)
  (:require (in? l (list "the-label" "auto-nr" "current-part" "language"
                         "page-nr" "page-the-page" "prog-language"
                         "caption-summarized" "figure-width")))
  #f)

(tm-define (style-clear-cache)
  (former)
  (focus-parameters-cache-clear))

(tm-menu (focus-parameters-menu t mode)
  (with ps (focus-parameters-list-memo t mode)
    (if (nnull? ps)
        (group "Style parameters")
        (for (p ps)
          (dynamic (focus-parameter-menu-item p mode)))
        (if (tree-label-extension? (tree-label t))
            ---))))

(tm-menu (focus-theme-parameters-submenu th mode)
  (with mems (theme->members th)
    (for (mem mems)
      (with var (string-append th "-" mem)
        (dynamic (focus-parameter-menu-item var mode))))))

(tm-menu (focus-theme-parameters-menu t mode)
  (with ths (search-tag-themes t)
    (if (nnull? ths)
        (group "Theme parameters")
        (for (th ths)
          (-> (eval th)
              (dynamic (focus-theme-parameters-submenu th mode))))
        ---)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main Focus menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (focus-ancestor-menu t))

(tm-menu (focus-toggle-menu t)
  (push-focus t
    (assuming (numbered-context? t)
      ;; FIXME: itemize, enumerate, eqnarray*
      ((check "Numbered" "v" (pull-focus t (numbered-numbered? t)))
       (pull-focus t (numbered-toggle t))))
    (assuming (alternate-context? t)
      ((check (eval (alternate-second-name t)) "v"
              (pull-focus t (alternate-second? t)))
       (pull-focus t (alternate-toggle t))))
    (assuming (!= (tree-children t) (tree-accessible-children t))
      ((check "Show hidden" "v" (pull-focus t (tree-is? t :up 'inactive)))
       (pull-focus t (inactive-toggle t))))))

(tm-menu (focus-float-menu t))
(tm-menu (focus-misc-menu t))

(tm-menu (focus-misc-menu t)
  (:require (tree-in? t '(hlink)))
  ("Convert to card" (display-link-as-card t)))

(tm-menu (focus-misc-menu t)
  (:require (tree-in? t '(cardlink)))
  ("Convert to hyperlink" (display-card-as-link t)))

(tm-menu (focus-style-options-menu t)
  (with opts (search-tag-options t)
    (if (nnull? opts)
        (group "Style options")
        (for (opt opts)
          ((check (balloon (eval (style-get-menu-name opt))
                           (eval (style-get-documentation opt))) "v"
                  (has-style-package? opt))
           (toggle-style-package opt)))
        (if (tree-label-extension? (tree-label t))
            ---))))

(tm-menu (focus-tag-edit-menu l)
  (if (tree-label-extension? l)
      (let* ((s (symbol->string l))
             (cmd (string-append "(make '" s ")")))
        (when (has-macro-source? l)
          ("Edit source" (edit-focus-macro-source)))
        (assuming (not (has-user-shortcut? cmd))
          ("Create shortcut" (open-shortcuts-editor "" cmd)))
        (assuming (has-user-shortcut? cmd)
          ("Edit shortcut" (open-shortcuts-editor "" cmd))))))

(tm-menu (focus-preferences-menu t)
  (dynamic (focus-style-options-menu t))
  (dynamic (focus-parameters-menu t :global))
  (dynamic (focus-theme-parameters-menu t :global))
  (dynamic (focus-tag-edit-menu (tree-label t))))

(tm-menu (focus-rendering-menu t)
  (dynamic (focus-parameters-menu t (list :local (tree-label t))))
  (dynamic (focus-theme-parameters-menu t (list :local (tree-label t)))))

(tm-menu (focus-search-menu t)
  ("Search in database" (focus-open-search-tool t)))

(tm-menu (focus-tag-menu t)
  (with l (focus-variants-of t)
    (assuming (<= (length l) 1)
      (inert ((eval (focus-tag-name (tree-label t))) (noop) (noop))))
    (assuming (> (length l) 1)
      (-> (eval (focus-tag-name (tree-label t)))
          (dynamic (focus-variant-menu t)))))
  (dynamic (focus-toggle-menu t))
  (dynamic (focus-float-menu t))
  (dynamic (focus-misc-menu t))
  (assuming (focus-has-preferences? t)
    (-> "Preferences"
        (dynamic (focus-preferences-menu t))))
  (assuming (focus-has-parameters? t)
    (-> "Rendering"
        (dynamic (focus-rendering-menu t))))
  ("Describe" (focus-help))
  ("Delete" (remove-structure-upwards))
  (assuming (focus-has-search-menu? t)
    (-> "Search" (dynamic (focus-search-menu t))))
  (assuming (focus-can-search? t)
    ("Search in database" (focus-open-search-tool t))))

(tm-menu (focus-move-menu t)
  ("Previous similar" (traverse-previous))
  ("Next similar" (traverse-next))
  ("First similar" (traverse-first))
  ("Last similar" (traverse-last))
  (assuming (cursor-inside? t)
    ("Exit left" (structured-exit-left))
    ("Exit right" (structured-exit-right))))

(tm-menu (focus-insert-menu t)
  (assuming (and (structured-horizontal? t) (not (structured-vertical? t)))
    (when (focus-can-insert? t)
      ("Insert argument before" (structured-insert-left))
      ("Insert argument after" (structured-insert-right)))
    (when (focus-can-remove? t)
      ("Remove argument before" (structured-remove-left))
      ("Remove argument after" (structured-remove-right))))
  (assuming (structured-vertical? t)
    ("Insert above" (structured-insert-up))
    ("Insert left" (structured-insert-left))
    ("Insert right" (structured-insert-right))
    ("Insert below" (structured-insert-down))
    ("Remove upwards" (structured-remove-up))
    ("Remove leftwards" (structured-remove-left))
    ("Remove rightwards" (structured-remove-right))
    ("Remove downwards" (structured-remove-down))))

(tm-menu (focus-extra-menu t))

(tm-define (hidden-inputter-children t)
  (append-map
    (lambda (i)
      (with c (tree-ref t i)
        (if (and (hidden-child? t i)
                 (with type (tree-child-type t i)
                   (inputter-active? c type)))
            (list c)
            (list))))
    (.. 0 (tree-arity t))))

(tm-menu (focus-hidden-menu t)
  (assuming (nnull? (hidden-inputter-children t))
    ---
    (for (i (.. 0 (tree-arity t)))
      (assuming (hidden-child? t i)
        (dynamic (string-input-menu t i))))))

(tm-menu (focus-hidden-menu t)
  (:require (pure-alternate-context? t)))

(tm-menu (focus-label-menu t)
  (assuming (focus-label t)
    ---
    (with s (focus-get-label t)
      ((eval (string-append "#" s))
       (interactive (lambda (l) (focus-set-label t l))
         (list "Label" "string" s))))))  

(tm-menu (standard-focus-menu t)
  (dynamic (focus-ancestor-menu t))
  (dynamic (focus-tag-menu t))
  (assuming (focus-can-move? t)
    ---
    (dynamic (focus-move-menu t)))
  (assuming (focus-can-insert-remove? t)
    ---
    (dynamic (focus-insert-menu t)))
  (dynamic (focus-hidden-menu t))
  (dynamic (focus-extra-menu t))
  (dynamic (focus-label-menu t)))

(tm-menu (focus-menu)
  (dynamic (standard-focus-menu (focus-tree))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The main focus icons bar
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (focus-ancestor-icons t))

(tm-menu (focus-toggle-icons t)
  (push-focus t
    (assuming (numbered-context? t)
      ((check (balloon (icon "tm_numbered") "Toggle numbering") "v"
              (pull-focus t (numbered-numbered? t)))
       (pull-focus t (numbered-toggle t))))
    (assuming (alternate-first? t)
      ((check (balloon (icon (eval (alternate-first-icon t)))
                       (eval (pull-focus t (alternate-second-name t)))) "v" #f)
       (pull-focus t (alternate-toggle t))))
    (assuming (alternate-second? t)
      ((check (balloon (icon (eval (alternate-second-icon t)))
                       (eval (pull-focus t (alternate-second-name t)))) "v" #t)
       (pull-focus t (alternate-toggle t))))
    (assuming (!= (tree-children t) (tree-accessible-children t))
      ((check (balloon (icon "tm_show_hidden") "Show hidden") "v"
              (pull-focus t (tree-is? t :up 'inactive)))
       (pull-focus t (inactive-toggle t))))))

(tm-menu (focus-float-icons t))
(tm-menu (focus-misc-icons t))
(tm-menu (focus-tag-extra-icons t))

(tm-menu (focus-tag-icons t)
  (dynamic (focus-toggle-icons t))
  (dynamic (focus-float-icons t))
  (dynamic (focus-misc-icons t))
  (mini #t
    (with l (focus-variants-of t)
      (assuming (<= (length l) 1)
        (inert ((eval (focus-tag-name (tree-label t))) (noop))))
      (assuming (> (length l) 1)
        (=> (balloon (eval (focus-tag-name (tree-label t)))
                     "Structured variant")
            (dynamic (focus-variant-menu t))))))
  (dynamic (focus-tag-extra-icons t))
  (assuming (cursor-inside? t)
    ((balloon (icon "tm_exit_left") "Exit tag on the left")
     (structured-exit-left))
    ((balloon (icon "tm_exit_right") "Exit tag on the right")
     (structured-exit-right))
    ((balloon (icon "tm_focus_delete") "Remove tag")
     (remove-structure-upwards)))
  (assuming (focus-has-preferences? t)
    (=> (balloon (icon "tm_focus_prefs") "Preferences for tag")
	(dynamic (focus-preferences-menu t))))
  (assuming (focus-has-parameters? t)
    (=> (balloon (icon "tm_theme") "Rendering options for tag")
        (dynamic (focus-rendering-menu t))))
  ((balloon (icon "tm_focus_help") "Describe tag")
   (focus-help))
  (assuming (focus-has-search-menu? t)
    (=> (balloon (icon "tm_focus_search") "Search")
        (dynamic (focus-search-menu t))))
  (assuming (focus-can-search? t)
    ((balloon (icon "tm_focus_search") "Search in database")
     (focus-open-search-tool t))))

(tm-menu (focus-move-icons t)
  ((balloon (icon "tm_similar_first") "Go to first similar tag")
   (traverse-first))
  ((balloon (icon "tm_similar_previous") "Go to previous similar tag")
   (traverse-previous))
  ((balloon (icon "tm_similar_next") "Go to next similar tag")
   (traverse-next))
  ((balloon (icon "tm_similar_last") "Go to last similar tag")
   (traverse-last)))

(tm-menu (focus-insert-icons t)
  (assuming (and (structured-horizontal? t) (not (structured-vertical? t)))
    (when (focus-can-insert? t)
      ((balloon (icon "tm_insert_left") "Structured insert at the left")
       (structured-insert-left))
      ((balloon (icon "tm_insert_right") "Structured insert at the right")
       (structured-insert-right)))
    (when (focus-can-remove? t)
      ((balloon (icon "tm_delete_left") "Structured remove leftwards")
       (structured-remove-left))
      ((balloon (icon "tm_delete_right") "Structured remove rightwards")
       (structured-remove-right))))
  (assuming (structured-vertical? t)
    ((balloon (icon "tm_insert_up") "Structured insert above")
     (structured-insert-up))
    ((balloon (icon "tm_insert_left") "Structured insert at the left")
     (structured-insert-left))
    ((balloon (icon "tm_insert_right") "Structured insert at the right")
     (structured-insert-right))
    ((balloon (icon "tm_insert_down") "Structured insert below")
     (structured-insert-down))
    ((balloon (icon "tm_delete_up") "Structured remove upwards")
     (structured-remove-up))
    ((balloon (icon "tm_delete_left") "Structured remove leftwards")
     (structured-remove-left))
    ((balloon (icon "tm_delete_right") "Structured remove rightwards")
     (structured-remove-right))
    ((balloon (icon "tm_delete_down") "Structured remove downwards")
     (structured-remove-down))))

(tm-menu (focus-extra-icons t))

(tm-menu (focus-hidden-icons t)
  (for (i (.. 0 (tree-arity t)))
    (assuming (hidden-child? t i)
      (dynamic (string-input-icon t i)))))

(tm-menu (focus-hidden-icons t)
  (:require (pure-alternate-context? t)))

(tm-menu (focus-label-icons t)
  (push-focus t
    (assuming (focus-label t)
      (with s (focus-get-label t)
        (glue #f #f 3 0)
        (mini #t (group "Label:"))
        (mini #t (input (pull-focus t (focus-set-label t answer)) "string"
                        (list s) "12em"))))))

(tm-menu (standard-focus-icons t)
  (dynamic (focus-ancestor-icons t))
  (assuming (focus-can-move? t)
    (minibar (dynamic (focus-move-icons t)))
    //)
  (assuming (focus-can-insert-remove? t)
    (minibar (dynamic (focus-insert-icons t)))
    //)
  (minibar (dynamic (focus-tag-icons t)))
  (dynamic (focus-hidden-icons t))
  (dynamic (focus-extra-icons t))
  (dynamic (focus-label-icons t))
  //)

(tm-menu (texmacs-focus-icons)
  (assuming (in-graphics?)
    (dynamic (graphics-focus-icons)))
  (assuming (not (in-graphics?))
    (dynamic (standard-focus-icons (focus-tree)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Focus menus for customizable environments
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-menu (focus-customizable-menu-item var name mode)
  (with setter (lambda (val) (parameter-set var val mode))
    ((eval name)
     (interactive setter (list name "string" (parameter-get var mode))))))

(tm-menu (focus-customizable-menu-item var name mode)
  (:require (parameter-choice-list var))
  (-> (eval name) (dynamic (parameter-submenu var mode))))

(tm-menu (focus-customizable-menu-item var name mode)
  (:require (== (tree-label-type (string->symbol var)) "color"))
  (-> (eval name) (dynamic (parameter-submenu var mode))))

(tm-menu (focus-extra-menu t)
  (:require (customizable-context? t))
  ---
  (for (p (customizable-parameters-memo t))
    (with (var name) p
      (with mode (list :local (tree-label t))
        (dynamic (focus-customizable-menu-item var name mode))))))

(tm-menu (focus-customizable-icons-item var name mode)
  (input (parameter-set var answer mode) "string"
         (list (parameter-get-string var mode)) "5em"))

(tm-menu (focus-customizable-icons-item var name mode)
  (:require (parameter-choice-list var))
  (mini #t
    (=> (eval (parameter-get-string var mode))
        (dynamic (parameter-submenu var mode)))))

(tm-menu (focus-customizable-icons-item var name mode)
  (:require (== (tree-label-type (string->symbol var)) "color"))
  (=> (color (parameter-get var mode) #f #f 24 16)
      (dynamic (parameter-submenu var mode))))

(tm-menu (focus-extra-icons t)
  (:require (customizable-context? t))
  (for (p (customizable-parameters-memo t))
    (with (var name) p
      (with mode (list :local (tree-label t))
        (glue #f #f 3 0)
        (mini #t (group (eval (string-append name ":"))))
        (dynamic (focus-customizable-icons-item var name mode))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Immediately load document-menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(import-from (generic document-menu))
