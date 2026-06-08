
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : document-widgets.scm
;; DESCRIPTION : widgets for setting global document properties
;; COPYRIGHT   : (C) 2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic document-widgets)
  (:use (generic document-menu)
        (generic format-widgets)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Style chooser widget (still to be implemented)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (select-style-among-widget l)
  (resize '("300px" "300px" "300px") '("200px" "300px" "1000px")
    (scrollable
      (choice (set-main-style answer) l "generic"))))

(tm-widget (select-common-style-widget)
  (dynamic (select-style-among-widget
            (list "article" "beamer" "book" "browser" "exam"
                  "generic" "letter" "manual" "seminar" "source"))))

(tm-widget (select-education-style-widget)
  (dynamic (select-style-among-widget
            (list "compact" "exam"))))

(tm-widget (select-article-style-widget)
  (dynamic (select-style-among-widget
            (list "article" "tmarticle"))))

(tm-widget (select-any-style-widget)
  (dynamic (select-style-among-widget
            (list "article" "tmarticle"))))

(tm-widget (select-style-widget)
  (tabs
    (tab (text "Common")
      (dynamic (select-common-style-widget)))
    (tab (text "Education")
      (dynamic (select-education-style-widget)))
    (tab (text "Article")
      (dynamic (select-article-style-widget)))
    (tab (text "Any")
      (dynamic (select-any-style-widget)))))

(tm-define (open-style-selector)
  (:interactive #t)
  (top-window select-style-widget "Select document style"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document -> Source -> Preferences
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget ((source-tree-preferences-editor u) quit)
  (padded
    (refreshable "source-tree-preferences"
      (aligned
        (item (text "Main presentation style:")
          (enum (initial-set u "src-style" answer)
                '("angular" "scheme" "functional" "latex")
                (initial-get u "src-style") "10em"))
        (item (text "Tags with special rendering:")
          (enum (initial-set u "src-special" answer)
                '("raw" "format" "normal" "maximal")
                (initial-get u "src-special") "10em"))
        (item (text "Compactification:")
          (enum (initial-set u "src-compact" answer)
                '("none" "inline" "normal" "inline args" "all")
                (initial-get u "src-compact") "10em"))
        (item (text "Closing style:")
          (enum (initial-set u "src-close" answer)
                '("repeat" "long" "compact" "minimal")
                (initial-get u "src-close") "10em"))))
    ======
    (explicit-buttons
      (hlist
        >>>
        ("Reset"
         (initial-default u "src-style" "src-special"
                            "src-compact" "src-close")
         (refresh-now "source-tree-preferences"))
        // //
        ("Ok" (quit))))))

(tm-define (open-source-tree-preferences-window)
  (:interactive #t)
  (with u (current-buffer)
    (dialogue-window (source-tree-preferences-editor u) noop
                     "Document source tree preferences")))

(tm-define (open-source-tree-preferences)
  (:interactive #t)
  (if (side-tools?)
      (tool-select :right 'source-tree-preferences-tool)
      (open-source-tree-preferences-window)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document -> Paragraph
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (open-document-paragraph-format-window)
  (:interactive #t)
  (let* ((old (get-init-table paragraph-parameters))
         (new (get-init-table paragraph-parameters))
         (u   (current-buffer)))
    (dialogue-window (paragraph-formatter old new init-multi u #t)
                     noop "Document paragraph format")))

(tm-define (open-document-paragraph-format)
  (:interactive #t)
  (if (side-tools?)
      (tool-select :right 'document-paragraph-tool)
      (open-document-paragraph-format-window)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document -> Page
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (open-document-page-format-window)
  (:interactive #t)
  (page-properties-pane-show))

(tm-define (open-document-page-format)
  (:interactive #t)
  (page-properties-pane-show))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document -> Metadata
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget ((document-metadata-editor u) quit)
  (padded
    (refreshable "document-metadata"
      (aligned
        (item (text "Title:")
          (input (initial-set u "global-title" answer) "string"
                 (list (buffer-get-metadata u "title")) "24em"))
        (item (text "Author:")
          (input (initial-set u "global-author" answer) "string"
                 (list (buffer-get-metadata u "author")) "24em"))
        (item (text "Subject:")
          (input (initial-set u "global-subject" answer) "string"
                 (list (buffer-get-metadata u "subject")) "24em"))
        (item (text "Created Time:")
          (input (initial-set u "global-created-time" answer) "string"
                 (list (buffer-get-metadata u "created-time")) "24em"))
        (item (text "Modified Time:")
          (input (initial-set u "global-modified-time" answer) "string"
                 (list (buffer-get-metadata u "modified-time")) "24em"))
        (item (text "Content Hash:")
          (input (initial-set u "global-content-hash" answer) "string"
                 (list (buffer-get-metadata u "content-hash")) "24em"))))
    ======
    (explicit-buttons
      (hlist
        >>>
        ("Reset"
         (initial-default u
                          "global-title" "global-author" "global-subject"
                          "global-created-time" "global-modified-time"
                          "global-content-hash")
         (refresh-now "document-metadata"))
        // //
        ("Ok" (quit))))))

(tm-define (open-document-metadata-window)
  (:interactive #t)
  (let* ((u (current-buffer)))
    (dialogue-window (document-metadata-editor u) noop "Document metadata")))

(tm-define (open-document-metadata)
  (:interactive #t)
  (if (side-tools?)
      (tool-select :right 'document-metadata-tool)
      (open-document-metadata-window)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Document -> Color
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-widget (page-colors-background u)
  (pick-background "" (initial-set-tree u "bg-color" answer)))

(tm-widget (page-colors-foreground u)
  (pick-color (initial-set-tree u "color" answer)))

(tm-widget ((document-colors-picker u) quit)
  (padded
    (refreshable "page-colors"
      (tabs
        (tab (text "Background")
          (padded
            (dynamic (page-colors-background u))))
        (tab (text "Foreground")
          (padded
            (dynamic (page-colors-foreground u))))))
    ======
    (explicit-buttons
      (hlist
        >>>
        ("Reset"
         (initial-default u "bg-color" "color")
         (refresh-now "page-colors"))
        // //
        ("Ok" (quit))))))

(tm-define (open-document-colors-window)
  (:interactive #t)
  (with u (current-buffer)
    (dialogue-window (document-colors-picker u) noop "Document colors")))

(tm-define (open-document-colors)
  (:interactive #t)
  (if (side-tools?)
      (tool-select :right 'document-colors-tool)
      (open-document-colors-window)))
