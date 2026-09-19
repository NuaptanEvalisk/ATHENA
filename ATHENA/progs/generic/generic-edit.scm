
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic editing via the keyboard
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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
;; Structured insert and remove
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Structured movements
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Special structured editing
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Tree editing
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Extra editing functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Standard environment parameters for primitives
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Inserting various kinds of content
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (make-experimental-build-warning)
  (:synopsis "Insert the ATHENA experimental build warning"))

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
;; Thumbnails facility
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (thumbnail-suffixes)
  (list->url
    (map url-wildcard
         '("*.gif" "*.jpg" "*.jpeg" "*.JPG" "*.JPEG" "*.png" "*.PNG"))))

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

(tm-property (in-main-flow?)
  (:synopsis "Are we inside the main document flow?"))

(tm-property (make-marginal-note)
  (:synopsis "Insert a marginal note"))

(tm-property (set-marginal-note-hpos hp)
  (:synopsis "Set the horizontal position of the marginal note to @hp")
  (:check-mark "v" test-marginal-note-hpos?))

(tm-property (set-marginal-note-valign va)
  (:synopsis "Set the vertical alignment of the marginal note to @va")
  (:check-mark "v" test-marginal-note-valign?))

(tm-property (make-insertion s)
  (:synopsis "Make an insertion of type @s")
  (:applicable (in-main-flow?)))

(tm-property (insertion-positioning what flag)
  (:synopsis "Allow/disallow the position @what for innermost float"))

(tm-property (toggle-insertion-positioning what)
  (:check-mark "v" test-insertion-positioning?))

(tm-property (toggle-insertion-positioning-not s)
  (:check-mark "v" not-test-insertion-positioning?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Balloons
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (balloon-context? t)
  (tree-in? t (balloon-tag-list)))

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

(tm-property (set-balloon-halign ha)
  (:synopsis "Set the horizontal alignment of the marginal note to @ha")
  (:check-mark "v" test-balloon-halign?))

(tm-property (set-balloon-valign va)
  (:synopsis "Set the vertical alignment of the marginal note to @va")
  (:check-mark "v" test-balloon-valign?))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Labels attached to markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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

(tm-property (focus-open-search-tool t)
  (:interactive #t))
