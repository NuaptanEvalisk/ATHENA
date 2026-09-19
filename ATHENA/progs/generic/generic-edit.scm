
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : generic-edit.scm
;; DESCRIPTION : Generic editing routines
;; COPYRIGHT   : (C) 2001  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic generic-edit)
  (:use (utils library tree)
	(utils library cursor)
	(utils edit variants)
        (utils misc tooltip)
	(source macro-search)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic cursor movements via the keyboard
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (kbd-horizontal t forwards?)
  (and-with p (tree-outer t)
    (kbd-horizontal p forwards?)))

(tm-define (kbd-vertical t downwards?)
  (and-with p (tree-outer t)
    (kbd-vertical p downwards?)))

(tm-define (kbd-extremal t forwards?)
  (and-with p (tree-outer t)
    (kbd-extremal p forwards?)))

(tm-define (kbd-incremental t downwards?)
  (and-with p (tree-outer t)
    (kbd-incremental p downwards?)))

(tm-define (kbd-horizontal t forwards?)
  (:require (tree-is-buffer? t))
  (with move (lambda () (if forwards? (go-right) (go-left)))
    (go-to-next-such-that move generic-context?)))

(tm-define (kbd-vertical t downwards?)
  (:require (tree-is-buffer? t))
  (with move (lambda () (if downwards? (go-down) (go-up)))
    (go-to-next-such-that move generic-context?)))

(tm-define (kbd-extremal t forwards?)
  (:require (tree-is-buffer? t))
  (with move (lambda () (if forwards? (go-end-line) (go-start-line)))
    (go-to-next-such-that move generic-context?)))

(tm-define (kbd-incremental t downwards?)
  (:require (tree-is-buffer? t))
  (with move (lambda () (if downwards? (go-page-down) (go-page-up)))
    (go-to-next-such-that move generic-context?)))

(tm-define (kbd-left-raw)
  (kbd-horizontal (focus-tree) #f))
(tm-define (kbd-right-raw)
  (kbd-horizontal (focus-tree) #t))
(tm-define (kbd-up-raw)
  (kbd-vertical (focus-tree) #f))
(tm-define (kbd-down-raw)
  (kbd-vertical (focus-tree) #t))
(tm-define (kbd-start-line-raw)
  (kbd-extremal (focus-tree) #f))
(tm-define (kbd-end-line-raw)
  (kbd-extremal (focus-tree) #t))
(tm-define (kbd-page-up-raw)
  (kbd-incremental (focus-tree) #f))
(tm-define (kbd-page-down-raw)
  (kbd-incremental (focus-tree) #t))

(tm-define (kbd-plain-move move)
  (select-from-keyboard #f)
  (move))

(tm-define (kbd-left) (kbd-plain-move kbd-left-raw))
(tm-define (kbd-right) (kbd-plain-move kbd-right-raw))
(tm-define (kbd-up) (kbd-plain-move kbd-up-raw))
(tm-define (kbd-down) (kbd-plain-move kbd-down-raw))
(tm-define (kbd-start-line) (kbd-plain-move kbd-start-line-raw))
(tm-define (kbd-end-line) (kbd-plain-move kbd-end-line-raw))
(tm-define (kbd-page-up) (kbd-plain-move kbd-page-up-raw))
(tm-define (kbd-page-down) (kbd-plain-move kbd-page-down-raw))

(tm-define (kbd-select r)
  (select-from-shift-keyboard)
  (r)
  (select-from-cursor))

(tm-define (kbd-select-if-active r)
  (r)
  (select-from-cursor-if-active))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic editing via the keyboard
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (insert-return) (insert-raw-return))

(tm-define (kbd-space)
  (kbd-space-bar (focus-tree) #f))
(tm-define (kbd-shift-space)
  (kbd-space-bar (focus-tree) #t))
(tm-define (kbd-return)
  (kbd-enter (focus-tree) #f))
(tm-define (kbd-shift-return)
  (kbd-enter (focus-tree) #t))
(tm-define (kbd-control-return)
  (kbd-control-enter (focus-tree) #f))
(tm-define (kbd-shift-control-return)
  (kbd-control-enter (focus-tree) #t))
(tm-define (kbd-alternate-return)
  (kbd-alternate-enter (focus-tree) #f))
(tm-define (kbd-shift-alternate-return)
  (kbd-alternate-enter (focus-tree) #t))
(tm-define (kbd-backspace)
  (kbd-remove (focus-tree) #f))
(tm-define (kbd-delete)
  (kbd-remove (focus-tree) #t))
(tm-define (kbd-tab)
  (kbd-variant (focus-tree) #t))
(tm-define (kbd-shift-tab)
  (kbd-variant (focus-tree) #f))
(tm-define (kbd-alternate-tab)
  (kbd-alternate-variant (focus-tree) #t))
(tm-define (kbd-shift-alternate-tab)
  (kbd-alternate-variant (focus-tree) #f))
(tm-define (kbd-copy)
  (clipboard-copy "primary"))
(tm-define (kbd-cut)
  (clipboard-cut "primary"))
(tm-define (kbd-paste)
  (clipboard-paste "primary"))
(tm-define (kbd-cancel)
  (clipboard-clear "primary"))

(tm-define (notify-activated t) (noop))
(tm-define (notify-disactivated t) (noop))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic gestures
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (swipe-horizontal t forward?)
  (and-with p (tree-outer t)
    (swipe-horizontal p forward?)))

(tm-define (swipe-vertical t down?)
  (and-with p (tree-outer t)
    (swipe-vertical p down?)))

(tm-define (swipe-left)
  (swipe-horizontal (focus-tree) #f))

(tm-define (swipe-right)
  (swipe-horizontal (focus-tree) #t))

(tm-define (swipe-up)
  (swipe-vertical (focus-tree) #f))

(tm-define (swipe-down)
  (swipe-vertical (focus-tree) #t))

(tm-define (structured-maximize t)
  (and-with p (tree-outer t)
    (structured-maximize p)))

(tm-define (structured-minimize t)
  (and-with p (tree-outer t)
    (structured-minimize p)))

(tm-define (wheel-capture?) #f)
(tm-define (wheel-event x y) (noop))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic predicates
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Focus predicates
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-has-variants? t)
  (> (length (focus-variants-of t)) 1))

(tm-define (focus-has-toggles? t)
  (or (numbered-context? t)
      (alternate-context? t)))

(tm-define (focus-can-move? t)
  #t)

(tm-define (focus-can-insert-remove? t)
  (and (or (structured-horizontal? t) (structured-vertical? t))
       (cursor-inside? t)))

(tm-define (focus-can-insert? t)
  (< (tree-arity t) (tree-maximal-arity t)))

(tm-define (focus-can-remove? t)
  (> (tree-arity t) (tree-minimal-arity t)))

(tm-define (focus-has-geometry? t)
  #f)

(tm-define (focus-has-parameters? t)
  (focus-has-preferences? t))

(tm-define (focus-can-search? t) #f)
(tm-define (focus-has-search-menu? t) #f)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Card links
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-secure-symbols system-icon-for-link)

(define (cardlink-destination-string destination)
  (tree->string destination))

(define (cardlink-has-extension? s extensions)
  (in? (string-downcase (url-suffix (string->url s))) extensions))

(define (cardlink-type destination)
  (let ((s (string-downcase (cardlink-destination-string destination))))
    (cond ((or (string-starts? s "http://")
               (string-starts? s "https://"))
           "Web")
          ((string-starts? s "tmfs://") "TMFS")
          ((string-ends? s "/") "Folder")
          ((string-ends? s ".pdf") "PDF")
          ((cardlink-has-extension? s '("png" "jpg" "jpeg" "gif" "svg" "webp"))
           "Image")
          ((cardlink-has-extension? s '("mp3" "ogg" "wav" "flac" "m4a"))
           "Audio")
          ((cardlink-has-extension? s '("mp4" "mkv" "mov" "webm" "avi"))
           "Video")
          ((cardlink-has-extension? s '("zip" "tar" "gz" "bz2" "xz" "7z"))
           "Archive")
          ((cardlink-has-extension? s '("txt" "md" "tm" "ath" "tex" "html" "htm"))
           "Text")
          ((cardlink-has-extension? s '("doc" "docx" "odt" "rtf" "ppt" "pptx"
                                         "odp" "xls" "xlsx" "ods"))
           "Office")
          (else "File"))))

(define (cardlink-type-display-name type)
  (cond ((== type "TMFS") "TMFS Link")
        (else (string-append type " Document"))))

(define (cardlink-type-default-link-name type)
  (cond ((== type "TMFS") "TMFS link")
        (else (string-append type " document"))))

(define (cardlink-icon destination type)
  (let ((icon (system-icon-for-link (cardlink-destination-string destination)
                                    type)))
    (if (!= icon "")
        `(image ,icon "1.35em" "" "" "")
        `(with "font-family" "ss"
               "font-series" "bold"
               "color" "#404040"
           ,(string-append "[" (upcase type) "]")))))

(define (cardlink-empty-body? body)
  (or (tm-equal? body "")
      (and (tree-atomic? body)
           (string-null? (tree->string body)))))

(define (cardlink-display-body body destination)
  (if (cardlink-empty-body? body)
      (cardlink-type-display-name (cardlink-type destination))
      body))

(define (cardlink-default-link-body destination)
  (cardlink-type-default-link-name (cardlink-type destination)))

(tm-define (ext-cardlink-render body destination)
  (:secure #t)
  (let* ((type (cardlink-type destination))
         (icon (cardlink-icon destination type))
         (display (cardlink-display-body body destination)))
    `(with "ornament-shape" "rectangular"
           "ornament-border" "1ln"
           "ornament-color" "#f8f8f8"
           "ornament-hpadding" "1spc"
           "ornament-vpadding" "0.75spc"
       (resize
         (ornament
           (concat ,icon "  " ,display))
         "" "" "" ""))))

(tm-define (display-link-as-card t)
  (tree-set! t `(cardlink ,(tree-ref t 0) ,(tree-ref t 1))))

(tm-define (display-card-as-link t)
  (let ((body (tree-ref t 0))
        (destination (tree-ref t 1)))
    (tree-set! t `(hlink ,(if (cardlink-empty-body? body)
                              (cardlink-default-link-body destination)
                              body)
                         ,destination))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Tree traversal
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (traverse-horizontal t forwards?)
  (if forwards? (go-to-next-word) (go-to-previous-word)))

(tm-define (traverse-vertical t downwards?)
  (and-with p (tree-outer t)
    (traverse-vertical p downwards?)))

(tm-define (traverse-vertical t downwards?)
  (:require (document-context? t))
  (with move (if downwards? go-to-next-tag go-to-previous-tag)
    (move 'document)))

(define (find-similar-upwards t l)
  (cond ((in? (tree-label t) l) t)
        ((and (not (tree-is-buffer? t)) (tree-up t))
         (find-similar-upwards (tree-up t) l))
        (else #f)))

(define-macro (with-focus-in l . body)
  `(begin
     ,@body
     (selection-cancel)
     (and-with t (find-similar-upwards (focus-tree) ,l)
       (tree-focus t))))

(tm-define (traverse-incremental t forwards?)
  (let* ((l (similar-to (tree-label t)))
         (fun (if forwards? go-to-next-tag go-to-previous-tag)))
    (with-focus-in l (fun l))))

(tm-define (traverse-extremal t forwards?)
  (let* ((l (similar-to (tree-label t)))
         (fun (if forwards? go-to-next-tag go-to-previous-tag))
         (inc (lambda () (fun l))))
    (with-focus-in l
      (go-to-repeat inc)
      (structured-inner-extremal t forwards?))))

(tm-define (traverse-previous)
  (traverse-incremental (focus-tree) #f))
(tm-define (traverse-next)
  (traverse-incremental (focus-tree) #t))
(tm-define (traverse-first)
  (traverse-extremal (focus-tree) #f))
(tm-define (traverse-last)
  (traverse-extremal (focus-tree) #t))
(tm-define (traverse-left)
  (traverse-horizontal (focus-tree) #f))
(tm-define (traverse-right)
  (traverse-horizontal (focus-tree) #t))
(tm-define (traverse-up)
  (traverse-vertical (focus-tree) #f))
(tm-define (traverse-down)
  (traverse-vertical (focus-tree) #t))
(tm-define (traverse-previous-section-title)
  (go-to-previous-tag (similar-to 'section)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured insert and remove
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (structured-insert-left)
  (structured-insert-horizontal (focus-tree) #f))
(tm-define (structured-insert-right)
  (structured-insert-horizontal (focus-tree) #t))
(tm-define (structured-remove-left)
  (structured-remove-horizontal (focus-tree) #f))
(tm-define (structured-remove-right)
  (structured-remove-horizontal (focus-tree) #t))
(tm-define (structured-insert-up)
  (structured-insert-vertical (focus-tree) #f))
(tm-define (structured-insert-down)
  (structured-insert-vertical (focus-tree) #t))
(tm-define (structured-remove-up)
  (structured-remove-vertical (focus-tree) #f))
(tm-define (structured-remove-down)
  (structured-remove-vertical (focus-tree) #t))
(tm-define (structured-insert-start)
  (structured-insert-extremal (focus-tree) #f))
(tm-define (structured-insert-end)
  (structured-insert-extremal (focus-tree) #t))
(tm-define (structured-insert-top)
  (structured-insert-incremental (focus-tree) #f))
(tm-define (structured-insert-bottom)
  (structured-insert-incremental (focus-tree) #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured movements
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (structured-left)
  (structured-horizontal (focus-tree) #f))
(tm-define (structured-right)
  (structured-horizontal (focus-tree) #t))
(tm-define (structured-up)
  (structured-vertical (focus-tree) #f))
(tm-define (structured-down)
  (structured-vertical (focus-tree) #t))
(tm-define (structured-start)
  (structured-extremal (focus-tree) #f))
(tm-define (structured-end)
  (structured-extremal (focus-tree) #t))
(tm-define (structured-top)
  (structured-incremental (focus-tree) #f))
(tm-define (structured-bottom)
  (structured-incremental (focus-tree) #t))
(tm-define (structured-exit-left)
  (structured-exit (focus-tree) #f))
(tm-define (structured-exit-right)
  (structured-exit (focus-tree) #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special structured editing
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (special-back)
  (special-navigate (focus-tree) :previous))
(tm-define (special-forward)
  (special-navigate (focus-tree) :next))
(tm-define (special-return)
  (special-navigate (focus-tree) :first))
(tm-define (special-shift-return)
  (special-navigate (focus-tree) :last))
(tm-define (special-left)
  (special-horizontal (focus-tree) #f))
(tm-define (special-right)
  (special-horizontal (focus-tree) #t))
(tm-define (special-up)
  (special-vertical (focus-tree) #f))
(tm-define (special-down)
  (special-vertical (focus-tree) #t))
(tm-define (special-first)
  (special-extremal (focus-tree) #f))
(tm-define (special-last)
  (special-extremal (focus-tree) #t))
(tm-define (special-previous)
  (special-incremental (focus-tree) #f))
(tm-define (special-next)
  (special-incremental (focus-tree) #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Tree editing
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extra editing functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (recenter-window)
  (set-scroll (get-cursor-x) (get-cursor-y))
  (refresh-window))

(tm-define (kill-paragraph)
  (selection-set-start)
  (go-end-paragraph)
  (selection-set-end)
  (clipboard-cut "primary"))

(tm-define (yank-paragraph)
  (selection-set-start)
  (go-end-paragraph)
  (selection-set-end)
  (clipboard-copy "primary"))

(tm-define (select-all)
  (tree-select (buffer-tree)))

(tm-define (go-to-line n . opt-from)
  (if (nnull? opt-from) (cursor-history-add (car opt-from)))
  (with-innermost t 'document
    (tree-go-to t n 0)))

(tm-define (go-to-column c . opt-from)
  (if (nnull? opt-from) (cursor-history-add (car opt-from)))
  (with-innermost t 'document
    (with p (tree-cursor-path t)
      (tree-go-to t (cADr p) c))))

(tm-define (select-word w t col)
  (:synopsis "Selects word @w in tree @t, more or less around column @col")
  (let* ((st (tree->string t))
         (pos (- col (string-length w)))
         (beg (string-contains st w (max 0 pos)))) ; returns index of w in st
    (if beg
        (with p (tree->path t)
          (go-to (rcons p beg))
          (selection-set-start)
          (go-to (rcons p (+ beg (string-length w))))
          (selection-set-end)))
    beg))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Standard environment parameters for primitives
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (search-parameters l)
  (:require (in? (if (string? l) l (symbol->string l))
                 '("reference" "pageref" "eqref" "smart-ref" "hlink")))
  (standard-parameters "locus"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Inserting various kinds of content
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (label-insert t)
  (and-with p (tree-outer t)
    (label-insert p)))

(tm-define (label-insert t)
  (:require (tree-is-buffer? t))
  (make 'label))

(tm-define (make-label)
  (label-insert (focus-tree)))

(tm-define (make-specific s)
  (if (or (== s "texmacs") (in-source?))
      (insert-go-to `(specific ,s "") '(1 0))
      (insert-go-to `(inactive (specific ,s "")) '(0 1 0))))

(tm-define (make-include u)
  (insert `(include ,(url->delta-unix u))))

(tm-define (make-experimental-build-warning)
  (:synopsis "Insert the ATHENA experimental build warning")
  (insert '(experimental-build-warning)))

(tm-define (make-inline-image l)
  (apply make-image (cons* (url->delta-unix (car l)) #f (cdr l))))

(tm-define (make-link-image l)
  (apply make-image (cons* (url->delta-unix (car l)) #t (cdr l))))

(tm-define (make-graphics-over-selection)
  (when (selection-active-any?)
    (with selection (selection-tree)
      (clipboard-cut "graphics background")
      (insert-go-to `(draw-over ,selection (graphics) "0cm") '(1 1)))))

(tm-define (make-graphics-over)
  (if (selection-active-any?)
      (with g `(with "gr-mode" (tuple "hand-edit" "penscript") (graphics))
        (with selection (selection-tree)
          (clipboard-cut "graphics background")
          (insert-go-to `(draw-over ,selection ,g "2cm") '(1 2 1))))
      (with g `(with "gr-mode" (tuple "hand-edit" "penscript") (graphics))
        (insert-go-to `(draw-over "" ,g "2cm") '(1 2 1)))))

(tm-define (make-anim l)
  (with duration "1s"
    (if (selection-active?)
        (let* ((selection (selection-tree))
	       (p (path-end selection (list))))
	  (when (selection-active-large?)
	    (set! selection `(par-block ,selection))
	    (set! p (cons 0 p)))
          (clipboard-cut "graphics background")
          (insert-go-to `(,l ,selection ,duration) (cons 0 p)))
        (insert-go-to `(,l "" ,duration) (list 0 0)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Detached notes
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (propose-note-id ref?)
  (let* ((buf (buffer-tree))
         (is-ref? (cut tree-in? <> '(note-ref note-ref*)))
         (is-text? (cut tree-in? <> '(note-inline note-inline*
                                      note-wide note-wide*
                                      note-footnote note-footnote*)))
         (ref-l (tree-search buf is-ref?))
         (text-l (tree-search buf is-text?))
         (ref-id (lambda (t) (tree->stree (tm-ref t 0))))
         (text-id (lambda (t) (tree->stree (tm-ref t 1))))
         (refs (map ref-id ref-l))
         (texts (map text-id text-l))
         (diff (if ref?
                   (list-difference texts refs)
                   (list-difference refs texts))))
    (if (null? diff)
        (create-unique-id)
        (cAr diff))))

(tm-define (make-note-ref)
  (insert `(note-ref ,(propose-note-id #t))))

(tm-define (make-note-inline)
  (insert-go-to `(note-inline "" ,(propose-note-id #f)) '(0 0)))

(tm-define (make-note-wide)
  (insert-go-to `(note-wide (document "") ,(propose-note-id #f)) '(0 0 0)))

(tm-define (make-note-footnote)
  (insert-go-to `(note-footnote (document "") ,(propose-note-id #f)) '(0 0 0)))
                                      
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Thumbnails facility
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (thumbnail-suffixes)
  (list->url
    (map url-wildcard
         '("*.gif" "*.jpg" "*.jpeg" "*.JPG" "*.JPEG" "*.png" "*.PNG"))))

(define (fill-row l nr)
  (cond ((= nr 0) '())
        ((nnull? l) (cons (car l) (fill-row (cdr l) (- nr 1))))
        (else (cons "" (fill-row l (- nr 1))))))

(define (make-rows l nr)
  (if (> (length l) nr)
      (cons (list-head l nr) (make-rows (list-tail l nr) nr))
      (list (fill-row l nr))))

(define (make-thumbnails-sub l nr)
  (let* ((w (string-append (number->string (- (/ 1.0 nr) 0.02)) "par"))
         (mapper (lambda (x) `(image ,(url->delta-unix x) ,w "" "" "")))       
         (l1 (map mapper l))
         (l2 (make-rows l1 nr))
         (l3 (map (lambda (r) `(row ,@(map (lambda (c) `(cell ,c)) r))) l2)))
    (insert `(tabular* (tformat (twith "table-width" "1par")
                                (twith "table-hyphen" "yes")
                                (table ,@l3))))))

(tm-define (make-thumbnails nr)
  (:argument nr "Number of pictures per row")
  (if (string? nr) (set! nr (min (string->number nr) 32)))
  (user-url "Picture directory" "directory" 
   (lambda (dir) 
     (let* ((find (url-append dir (thumbnail-suffixes)))
                  (files (url->list (url-expand (url-complete find "r"))))
                  (base (buffer-master))
                  (rel-files (map (lambda (x) (url-delta base x)) files)))
           (if (nnull? rel-files) (make-thumbnails-sub rel-files nr))))))
   
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Routines for floats
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (mini-flow-context? t)
  (tree-in? t (mini-flow-tag-list)))

(tm-define (in-main-flow?)
  (:synopsis "Are we inside the main document flow?")
  ;; FIXME: this routine can be improved quite a lot
  ;; we might make this property part of the DRD
  (not (tree-innermost mini-flow-context?)))

(tm-define (make-marginal-note)
  (:synopsis "Insert a marginal note")
  (wrap-selection-small
    (insert-go-to `(inactive (marginal-note "normal" "c" "")) '(0 2 0))))

(tm-define (test-marginal-note-hpos? hp)
  (and-with t (tree-innermost 'marginal-note #t)
    (tm-equal? (tree-ref t 0) hp)))
(tm-define (set-marginal-note-hpos hp)
  (:synopsis "Set the horizontal position of the marginal note to @hp")
  (:check-mark "v" test-marginal-note-hpos?)
  (and-with t (tree-innermost 'marginal-note #t)
    (tree-set t 0 hp)))

(tm-define (test-marginal-note-valign? va)
  (and-with t (tree-innermost 'marginal-note #t)
    (tm-equal? (tree-ref t 1) va)))
(tm-define (set-marginal-note-valign va)
  (:synopsis "Set the vertical alignment of the marginal note to @va")
  (:check-mark "v" test-marginal-note-valign?)
  (and-with t (tree-innermost 'marginal-note #t)
    (tree-set t 1 va)))

(tm-define (make-insertion s)
  (:synopsis "Make an insertion of type @s")
  (:applicable (in-main-flow?))
  (with pos (if (== s "float") "tbh" "")
    (insert-go-to (list 'float s pos (list 'document ""))
                  (list 2 0 0))))

(define (any-float? t)
  (tree-in? t '(float wide-float phantom-float)))

(tm-define (insertion-positioning what flag)
  (:synopsis "Allow/disallow the position @what for innermost float")
  (and-with t (tree-innermost any-float? #t)
    (let ((op (if flag string-union string-minus))
          (st (tree-ref t 1)))
      (tree-set! st (op (tree->string st) what)))))

(define (test-insertion-positioning? what)
  (and-with t (tree-innermost any-float? #t)
    (with c (string-ref what 0)
      (char-in-string? c (tree->string (tree-ref t 1))))))

(define (not-test-insertion-positioning? s)
  (not (test-insertion-positioning? s)))

(tm-define (toggle-insertion-positioning what)
  (:check-mark "v" test-insertion-positioning?)
  (insertion-positioning what (not-test-insertion-positioning? what)))

(tm-define (toggle-insertion-positioning-not s)
  (:check-mark "v" not-test-insertion-positioning?)
  (toggle-insertion-positioning s))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Balloons
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (balloon-context? t)
  (tree-in? t (balloon-tag-list)))

(define (integer-floor x)
  (inexact->exact (floor x)))

(tm-define (display-balloon body balloon halign valign type)
  (:secure #t)
  (let* ((kind (or (tm->string type) "default"))
         (ha (or (tm->string halign) (if (== kind "mouse") "right" "left")))
         (va (or (tm->string valign) "Bottom"))
         (p  (tree->path body))
         (st (tree->stree body))
         (id (or (list p st) st)))
    (show-tooltip id body balloon ha va kind 0.833333)))

(tm-define (make-balloon)
  (:synopsis "Insert a balloon")
  (wrap-selection-small
    (insert-go-to `(inactive (hover-balloon "" "" "left" "Bottom"))
                  '(0 0 0))))

(tm-define (test-balloon-halign? ha)
  (and-with t (tree-innermost balloon-context? #t)
    (tm-equal? (tree-ref t 2) ha)))
(tm-define (set-balloon-halign ha)
  (:synopsis "Set the horizontal alignment of the marginal note to @ha")
  (:check-mark "v" test-balloon-halign?)
  (and-with t (tree-innermost balloon-context? #t)
    (tree-set t 2 ha)))

(tm-define (test-balloon-valign? va)
  (and-with t (tree-innermost balloon-context? #t)
    (tm-equal? (tree-ref t 3) va)))
(tm-define (set-balloon-valign va)
  (:synopsis "Set the vertical alignment of the marginal note to @va")
  (:check-mark "v" test-balloon-valign?)
  (and-with t (tree-innermost balloon-context? #t)
    (tree-set t 3 va)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Labels attached to markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (focus-label t) #f)

(tm-define (focus-get-label t)
  (and-with l (focus-label t)
    (tm->string (tm-ref l 0))))

(tm-define (focus-set-label t val)
  (and-with l (focus-label t)
    (tree-set l 0 val)))

(tm-define (focus-list-search-label l)
  (and (nnull? l)
       (or (focus-search-label (car l))
           (focus-list-search-label (cdr l)))))

(tm-define (focus-search-label t)
  (cond ((tm-func? t 'label 1) t)
        ((tm-in? t '(document concat table row cell))
         (focus-list-search-label (tm-children t)))
        ((tm-in? t '(tformat with surround))
         (focus-search-label (cAr (tm-children t))))
        (else #f)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special keyboard behaviour when entering hybrid commands
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (hybrid-kbd-space)
  (activate-hybrid #f)
  (insert " "))

(tm-define (hybrid-kbd-formula-open bracket)
  (with-innermost t 'hybrid
    (with cmd (tm->string (tm-ref t 0))
      (if (== cmd "")
          (begin
            (tree-set t 0 bracket)
            (activate-hybrid #f))
          (insert bracket)))))

(tm-define (hybrid-kbd-curly-left)
  (with-innermost t 'hybrid
    (with cmd (tm->string (tm-ref t 0))
      (cond ((== cmd "")
             (tree-set t 0 "eqnarray")
             (activate-hybrid #f))
            ((or (not cmd) (== cmd "begin"))
             (insert "{"))
            ((in? cmd '("left\\" "right\\"))
             (insert "{")
             (activate-hybrid #f))
            (else
             (activate-hybrid #f))))))

(tm-define (hybrid-kbd-curly-right)
  (with-innermost t 'hybrid
    (with cmd (tm->string (tm-ref t 0))
      (cond ((not cmd)
             (activate-hybrid #f))
            ((string-starts? (tm->string cmd) "begin{")
             (tree-remove (tm-ref t 0) 0 6)
             (activate-hybrid #f))
            ((in? cmd '("left\\" "right\\"))
             (insert "}")
             (activate-hybrid #f))
            (else
             (activate-hybrid #f))))))

(tm-define (hybrid-kbd-backslash)
  (with-innermost t 'hybrid
    (with cmd (tm->string (tm-ref t 0))
      (cond ((in? cmd '("left" "right"))
             (insert "\\"))
            (else
             (activate-hybrid #f)
             (make-hybrid))))))

(tm-define (hybrid-kbd-sub)
  (activate-hybrid #f)
  (make-script #f #t))

(tm-define (hybrid-kbd-sup)
  (activate-hybrid #f)
  (make-script #t #t))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Search, replace, spell and tab-completion
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (key-press-command key)
  ;; FIXME: this routine should do exactly the same as key-press,
  ;; without modification of the internal state and without executing
  ;; the actual shortcut. It should rather return a command which
  ;; does all this, or #f
  (and-with p (kbd-find-key-binding key)
    (car p)))

(tm-define (keyboard-press key time)
  (:mode search-mode?)
  (with cmd (key-press-command (string-append "search " key))
    (cond (cmd (cmd))
          ((key-press-search key) (noop))
          (else (key-press key)))))

(tm-define (search-next)
  (key-press-search "next"))

(tm-define (search-previous)
  (key-press-search "previous"))

(tm-define (keyboard-press key time)
  (:mode spell-mode?)
  (with cmd (key-press-command (string-append "spell " key))
    (cond (cmd (cmd))
          ((key-press-spell key) (noop))
          (else (key-press key)))))

(tm-define (keyboard-press key time)
  (:mode complete-mode?)
  (with cmd (key-press-command (string-append "complete " key))
    (cond (cmd (cmd))
          ((key-press-complete key) (noop))
          (else (key-press key)))))

(tm-define (keyboard-press key time)
  (:mode remote-control-mode?)
  ;;(display* "Press " key "\n")
  (if (ahash-ref remote-control-remap key)
      (begin
        ;;(display* "Remap " (ahash-ref remote-control-remap key) "\n")
        (key-press (ahash-ref remote-control-remap key)))
      (key-press key)))

(tm-define (focus-open-search-tool t)
  (:interactive #t)
  (noop))
