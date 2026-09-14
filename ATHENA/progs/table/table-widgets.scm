
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : table-widgets.scm
;; DESCRIPTION : native table and cell property pane bridges
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (table table-widgets))

(tm-define (open-cell-properties)
  (:interactive #t)
  (cell-properties-pane-show))

(tm-define (open-table-properties)
  (:interactive #t)
  (table-properties-pane-show))
