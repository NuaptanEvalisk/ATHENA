
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : graphics-kbd.scm
;; DESCRIPTION : keyboard handling for graphics mode
;; COPYRIGHT   : (C) 2007  Joris van der Hoeven and Henri Lesourd
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (graphics graphics-kbd)
  (:use (generic generic-kbd)
        (utils library cursor)
        (graphics graphics-main)
        (graphics graphics-utils)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Various contexts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (in-active-graphics?)
  (and (in-graphics?)
       (not (in-commutative-diagram?))
       (== (get-env "preamble") "false")))

(define (in-beamer-graphics?)
  (and (in-active-graphics?) (in-screens?)))

(define (graphics-context? t)
  (tree-is? t 'graphics))

(define (inside-graphics-context? t)
  (tree-search-upwards t graphics-context?))

(define (inside-graphical-text-context? t)
  (and-with p (tree-ref t :up)
    (and-with i (tree-index t)
      (and (tree-accessible-child? p i)
           (and-with u (tree-search-upwards p graphical-text-context?)
             (inside-graphics-context? u))))))

(tm-define (generic-context? t)
  (:require (inside-graphics-context? t))
  #f)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Keyboard handling
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode in-active-graphics?)
  ("#" (graphics-toggle-grid))
  ("return" (graphics-apply-props-at-mouse))
  ("S-return" (graphics-get-props-at-mouse))
  ("C-g" (graphics-toggle-logical-grid))
  ("C-G" (graphics-toggle-visual-grid))
  ("C-2" (graphics-set-grid-aspect 'detailed 2 #t))
  ("C-3" (graphics-set-grid-aspect 'detailed 3 #t))
  ("C-4" (graphics-set-grid-aspect 'detailed 4 #t))
  ("C-5" (graphics-set-grid-aspect 'detailed 5 #t))
  ("C-6" (graphics-set-grid-aspect 'detailed 6 #t))
  ("C-7" (graphics-set-grid-aspect 'detailed 7 #t))
  ("C-8" (graphics-set-grid-aspect 'detailed 8 #t))
  ("C-9" (graphics-set-grid-aspect 'detailed 9 #t))
  ("C-0" (graphics-set-grid-aspect 'detailed 10 #t))
  ("C-left" (graphics-rotate-xz -0.1))
  ("C-right" (graphics-rotate-xz 0.1))
  ("C-up" (graphics-rotate-yz 0.1))
  ("C-down" (graphics-rotate-yz -0.1)))

(kbd-map
  (:mode in-beamer-graphics?)
  ("pageup" (screens-switch-to :previous))
  ("pagedown" (screens-switch-to :next)))

(define graphics-keys
  '("0" "#" "!"
    "home" "end" "pageup" "pagedown"
    "return" "backspace" "delete" "tab"
    "F1" "F2" "F3" "F4" "F9" "F10" "F11" "F12"))

(tm-define (keyboard-press key time)
  (:mode in-active-graphics?)
  (cond ((string-occurs? "-" key) (key-press key))
        ((in? key graphics-keys) (key-press key))))

(tm-define (geometry-vertical t down?)
  (:require (in-active-graphics?))
  (graphics-change-geo-valign down?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Text at 
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (kbd-horizontal t forwards?)
  (:require (graphical-text-context? t))
  (with-define (move) ((if forwards? go-right go-left))
    (go-to-next-inside move inside-graphical-text-context?)))

(tm-define (kbd-vertical t downwards?)
  (:require (graphical-text-context? t))
  (with-define (move) ((if downwards? go-down go-up))
    (go-to-next-inside move inside-graphical-text-context?)))

(tm-define (kbd-extremal t forwards?)
  (:require (graphical-text-context? t))
  (and-with c (tree-down t)
    (tree-go-to c (if forwards? :end :start))))

(tm-define (geometry-horizontal t forwards?)
  (:require (graphical-text-context? t))
  (let* ((old (graphical-get-attribute t "text-at-halign"))
         (new (if forwards?
                  (cond ((== old "right") "center")
                        (else "left"))
                  (cond ((== old "left") "center")
                        (else "right")))))
    (graphical-set-attribute t "text-at-halign" new)))

(tm-define (geometry-vertical t down?)
  (:require (graphical-text-context? t))
  (let* ((valign-var (graphics-valign-var t))
         (old (graphical-get-attribute t valign-var))
         (new (if down?
                  (cond ((== old "bottom") "base")
                        ((== old "base") "axis")
                        ((== old "axis") "center")
                        (else "top"))
                  (cond ((== old "top") "center")
                        ((== old "center") "axis")
                        ((== old "axis") "base")
                        (else "bottom")))))
    (graphical-set-attribute t valign-var new)))

(tm-define (geometry-extremal t forwards?)
  (:require (graphical-text-context? t))
  (graphical-set-attribute t "text-at-halign"
                           (if forwards? "left" "right")))

(tm-define (geometry-incremental t down?)
  (:require (graphical-text-context? t))
  (graphical-set-attribute t (graphics-valign-var t)
                           (if down? "top" "bottom")))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Draw over / draw under
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(kbd-map
  (:mode inside-graphical-over-under?)
  ("C-*" (graphics-toggle-over-under)))
