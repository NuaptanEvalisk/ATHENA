
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : preferences-widgets.scm
;; DESCRIPTION : thin Scheme bridge for native C++ preferences
;; COPYRIGHT   : (C) 2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena menus preferences-widgets))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Plugin preferences
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

(tm-widget (plugin-titled-preferences-widget name)
  (division "title"
    (text (string-append (plugin->name name) " preferences")) >>)
  (padded
    (dynamic (plugin-preferences-widget name))))

(tm-define (open-plugin-preferences name)
  (:interactive #t)
  (prefs-plugin-set name)
  (top-window plugin-preferences-widget*
              (string-append (plugin->name name) " preferences")))

(tm-define (open-plugins-preferences)
  (:interactive #t)
  (and-with l (plugins-with-preferences)
    (prefs-plugin-set (car l)))
  (top-window plugins-preferences-widget "Plugin preferences"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Native preferences entry points
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (preferences-open?)
  (native-preferences-open?))

(tm-define (open-preferences-window)
  (:interactive #t)
  (native-open-preferences))

(tm-define (open-preferences)
  (:interactive #t)
  (open-preferences-window))
