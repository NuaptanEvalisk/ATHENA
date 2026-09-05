
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : view-menu.scm
;; DESCRIPTION : the view menu
;; COPYRIGHT   : (C) 1999  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus view-menu)
  (:use
    (athena athena tm-view)
    (athena athena tm-server)
    (athena athena tm-files)
    (athena athena tm-reverse-hierarchy-graph)
    (athena menus view-widgets)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extra toolbars at the bottom
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define toolbar-replace-active? #f)
(tm-define toolbar-spell-active? #f)
(tm-define toolbar-animate-active? #f)

(tm-define (extra-bottom-tools?)
  (or toolbar-replace-active?
      toolbar-spell-active?
      toolbar-animate-active?))

(tm-widget (texmacs-bottom-toolbars)
  (if toolbar-replace-active?
      (link replace-toolbar))
  (if (and toolbar-spell-active?
           (not toolbar-replace-active?))
      (link spell-toolbar))
  (if (and toolbar-animate-active?
           (not toolbar-replace-active?)
           (not toolbar-spell-active?))
      (link animate-toolbar)))

(tm-define (test-bottom-bar? which)
  (cond ((== which "replace")
         toolbar-replace-active?)
        ((== which "spell")
         (and toolbar-spell-active?
              (not toolbar-replace-active?)))
        ((== which "animate")
         (and toolbar-animate-active?
              (not toolbar-replace-active?)
              (not toolbar-spell-active?)))
        (else #f)))

(tm-define (set-bottom-bar which val)
  (set! toolbar-replace-active? #f)
  (set! toolbar-spell-active? #f)
  (set! toolbar-animate-active? #f)
  (cond ((== which "replace")
         (set! toolbar-replace-active? val))
        ((== which "spell")
         (set! toolbar-spell-active? val))
        ((== which "animate")
         (set! toolbar-animate-active? val)))
  (update-bottom-tools))

(tm-define (toggle-bottom-bar which)
  (:check-mark "*" test-bottom-bar?)
  (set-bottom-bar which (not (test-bottom-bar? which))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The View menu
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(menu-bind view-menu
  ("Full screen mode"  (toggle-full-screen-edit-mode))
  ("Presentation mode" (toggle-full-screen-mode))
  ("Show panorama" (toggle-panorama-mode))
  ("Show all slides" (toggle-slideshow-mode))
  ("Show outline" (outline-pane-show))
  (link athena-view-panes-menu)
  ("Neighborhoods" (neighborhoods-pane-show))
  ("Error messages" (error-messages-show))
  ("Artifacts" (artifacts-pane-show))
  (-> "Headings"
      ("Unfold all" (heading-unfold-all)))
  ---
  ("Fit to screen" (fit-to-screen))  ("Fit to screen width" (fit-to-screen-width))
  ("Persistent fit width" (toggle-persistent-fit-width))
  ("Typewriter mode" (toggle-typewriter-mode))
  (-> "Labels"
      ("Visible" (set-preference "vault labels mode" "visible"))
      ("Small" (set-preference "vault labels mode" "small"))
      ("Hidden" (set-preference "vault labels mode" "hidden")))
  (-> "Graphs"
      ("Global hierarchy graph" (open-global-hierarchy-graph))
      ("Reverse hierarchy graph" (open-reverse-hierarchy-graph))
      ("Direct hierarchy graph" (open-direct-hierarchy-graph))
      ---
      ("Local reference graph" (open-local-reference-graph))
      ("Reference graph" (open-reference-graph)))
  ;;("Fit to screen height" (fit-to-screen-height))
  ("Zoom in" (zoom-in (sqrt (sqrt 2.0))))
  ("Zoom out" (zoom-out (sqrt (sqrt 2.0))))
  (-> "Zoom"
      ("50%"  (change-zoom-factor 0.5))
      ("71%"  (change-zoom-factor (sqrt 0.5)))
      ("100%" (change-zoom-factor 1.0))
      ("141%" (change-zoom-factor (sqrt 2.0)))
      ("200%" (change-zoom-factor 2.0))
      ---
      ("Other" (interactive other-zoom-factor)))

  ("Snap to pages" (toggle-snap-to-pages)))
