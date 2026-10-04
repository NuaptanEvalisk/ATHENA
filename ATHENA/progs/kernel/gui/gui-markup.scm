
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : gui-markup.scm
;; DESCRIPTION : Macros and functions for content generation
;; COPYRIGHT   : (C) 2010  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (kernel gui gui-markup)
  (:use (kernel regexp regexp-match)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Control structures
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (gui-normalize l)
  (cond ((null? l) l)
        ((func? (car l) 'list)
         (append (gui-normalize (cdar l)) (gui-normalize (cdr l))))
        (else (cons (car l) (gui-normalize (cdr l))))))

(tm-define-macro ($list . l)
  (:synopsis "Make widgets")
  `(gui-normalize (list ,@l)))

(tm-define-macro ($begin . l)
  (:synopsis "Begin primitive for content generation")
  `(cons* 'list ($list ,@l)))

(tm-define-macro ($if pred? . l)
  (:synopsis "When primitive for content generation")
  (cond ((== (length l) 1)
         `(cons* 'list (if ,pred? ($list ,(car l)) '())))
        ((== (length l) 2)
         `(cons* 'list (if ,pred? ($list ,(car l)) ($list ,(cadr l)))))
        (else
          (texmacs-error "$if" "invalid number of arguments"))))

(tm-define-macro ($when pred? . l)
  (:synopsis "When primitive for content generation")
  `(cons* 'list (if ,pred? ($list ,@l) '())))

(tm-define-macro ($for* var-vals . l)
  (:synopsis "For primitive for content generation")
  `(list 'for
         (lambda (,(car var-vals)) ($list ,@l))
         (lambda () ,(cadr var-vals))))

(tm-define (cond$sub l)
  (cond ((null? l)
         (list `(else '())))
        ((npair? (car l))
         (texmacs-error "cond$sub" "syntax error ~S" l))
        ((== (caar l) 'else)
         (list `(else ($list ,@(cdar l)))))
        (else (cons `(,(caar l) ($list ,@(cdar l)))
                    (cond$sub (cdr l))))))

(tm-define-macro ($cond . l)
  (:synopsis "Cond primitive for content generation")
  `(cons* 'list (cond ,@(cond$sub l))))

(tm-define-macro ($let decls . l)
  (:synopsis "Let* primitive for content generation")
  `(let ,decls
     (cons* 'list ($list ,@l))))

(tm-define-macro ($let* decls . l)
  (:synopsis "Let* primitive for content generation")
  `(let* ,decls
     (cons* 'list ($list ,@l))))

(tm-define-macro ($with var val . l)
  (:synopsis "With primitive for content generation")
  (if (string? var)
      ($quote `(with ,var ,val ($unquote ($inline ,@l))))
      `(with ,var ,val
         (cons* 'list ($list ,@l)))))

(tm-define-macro ($execute cmd . l)
  (:synopsis "Execute one command")
  `(begin
     ,cmd
     (cons* 'list ($list ,@l))))

(tm-define-macro ($for var-val . l)
  (:synopsis "For primitive for content generation")
  (when (nlist-2? var-val)
    (texmacs-error "$for" "syntax error in ~S" var-val))
  (with fun `(lambda (,(car var-val)) ($list ,@l))
    `(cons* 'list (append-map ,fun ,(cadr var-val)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic text markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define $lf '$lf)

(tm-define (markup-build-atom x)
  (cond ((number? x) (number->string x))
        ((symbol? x) (symbol->string x))
        ((== x #t) "true")
        ((== x #f) "false")
        (else x)))

(tm-define (markup-build-concat l)
  (with r (map markup-build-atom l)
    (cond ((null? r) "")
          ((list-1? r) (car r))
          (else (cons 'concat r)))))

(tm-define (markup-build-paragraphs l block?)
  (with s (list-scatter l (lambda (x) (== x '$lf)) #f)
    (with r (map markup-build-concat s)
      (cond ((and (null? r) block?) '(document ""))
            ((null? r) "")
            ((and (list-1? r) (not block?)) (car r))
            (else (cons 'document r))))))

(tm-define (markup-expand-document x)
  (if (tm-is? x 'document)
      (append-map (lambda (x) (list x $lf)) (tm-cdr x))
      (list x)))

(tm-define (markup-build-document l block?)
  (with x (append-map markup-expand-document l)
    (with y (if (and (nnull? x) (== (cAr x) $lf)) (cDr x) x)
      (markup-build-paragraphs y block?))))

(tm-define-macro ($textual . l)
  `(markup-build-document ($list ,@l) #f))

(tm-define-macro ($inline . l)
  `(markup-build-document ($list ,@l) #f))

(tm-define-macro ($block . l)
  `(markup-build-document ($list ,@l) #t))

(define (replace-unquotes x)
  (cond ((npair? x) x)
        ((== (car x) '$unquote) (cons 'unquote (cdr x)))
        (else (cons (replace-unquotes (car x)) (replace-unquotes (cdr x))))))

(tm-define ($quote x)
  (list 'quasiquote (replace-unquotes x)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Basic markup
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define-macro ($para . l)
  ($quote `(document ($unquote ($block ,@l)))))

(tm-define-macro ($itemize . l)
  ($quote `(document (itemize ($unquote ($block ,@l))))))

(tm-define-macro ($enumerate . l)
  ($quote `(document (enumerate ($unquote ($block ,@l))))))

(tm-define-macro ($description . l)
  ($quote `(document (description ($unquote ($block ,@l))))))

(tm-define-macro ($description-aligned . l)
  ($quote `(document (description-aligned ($unquote ($block ,@l))))))

(tm-define-macro ($description-long . l)
  ($quote `(document (description-long ($unquote ($block ,@l))))))

(tm-define-macro ($item)
  ($quote `(item)))

(tm-define-macro ($item* . l)
  ($quote `(item* ($unquote ($inline ,@l)))))

(tm-define-macro ($list-item . l)
  `($begin ($item) ,@l $lf))

(tm-define-macro ($describe-item key . l)
  `($begin ($item* ,key) ,@l $lf))

(tm-define-macro ($strong . l)
  ($quote `(strong ($unquote ($inline ,@l)))))

(tm-define-macro ($ismall . l)
  ($quote `(small (with "font-shape" "italic" ($unquote ($inline ,@l))))))

(tm-define-macro ($verbatim . l)
  ($quote `(verbatim ($unquote ($inline ,@l)))))

(tm-define-macro ($link dest . l)
  ($quote `(hlink ($unquote ($inline ,@l)) ($unquote ($textual ,dest)))))

(tm-define-macro ($color col . l)
  ($quote `(with "color" ,col ($unquote ($inline ,@l)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Specific markup for TeXmacs documentation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define-macro ($generic . l)
  ($quote
   `(document
      (style (tuple "generic"))
      (body ($unquote ($block ,@l))))))

(tm-define-macro ($tmdoc . l)
  ($quote
    `(document
       (style (tuple "tmdoc" "english"))
       (body ($unquote ($block ,@l))))))

(tm-define-macro ($localize . l)
  `($inline ,@l))

(tm-define-macro ($tmdoc-title . l)
  ($quote `(document (tmdoc-title ($unquote ($inline ,@l))))))

(tm-define-macro ($tmfs-title . l)
  ($quote `(document (tmfs-title ($unquote ($inline ,@l))))))

(tm-define-macro ($folded key . l)
  ($quote `(document (folded ($unquote ($inline ,key))
                             ($unquote ($block ,@l))))))

(tm-define-macro ($unfolded key . l)
  ($quote `(document (unfolded ($unquote ($inline ,key))
                               ($unquote ($block ,@l))))))

(tm-define-macro ($folded-documentation key . l)
  ($quote `(document (folded-documentation ($unquote ($inline ,key))
                                           ($unquote ($block ,@l))))))

(tm-define-macro ($unfolded-documentation key . l)
  ($quote `(document (unfolded-documentation ($unquote ($inline ,key))
                                             ($unquote ($block ,@l))))))

(tm-define-macro ($explain key . l)
  ($quote `(document (explain ($unquote ($inline ,key))
                              ($unquote ($block ,@l))))))

(tm-define-macro ($tm-fragment . l)
  ($quote `(document (tm-fragment ($unquote ($block ,@l))))))

(tm-define-macro ($markup . l)
  ($quote `(markup ($unquote ($inline ,@l)))))

(tm-define-macro ($tmstyle . l)
  ($quote `(tmstyle ($unquote ($inline ,@l)))))

(tm-define-macro ($shortcut cmd)
  ($quote `(shortcut ($unquote (object->string ',cmd)))))

(tm-define-macro ($tmdoc-link dest . l)
  `(with s (string-append "$ATHENA_DOC_PATH/" ,dest ".en.tm")
     ($link s ,@l)))

(tm-define-macro ($menu . l)
  `(list 'menu ,@l))

(tm-define-macro ($tmdoc-icon dest)
  ($quote `(icon ($unquote ($textual ,dest)))))

(tm-define-macro ($src-arg s)
  ($quote `(src-arg ($unquote ($textual ,s)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Graphics
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define-macro ($geometry w h unit . l)
  `(list 'with
         "gr-geometry" (list 'tuple "geometry" ,w ,h "center")
         "gr-frame" (list 'tuple "scale" ,unit
                          (list 'tuple "0.5gw" "0.5gh"))
         ($inline ,@l)))

(tm-define-macro ($auto-crop . l)
  `(list 'with
         "gr-auto-crop" "true"
         ($inline ,@l)))

(tm-define-macro ($grid unit . l)
  `(list 'with
         "gr-grid" (list 'tuple "cartesian" (list 'point "0" "0")
                         ,(markup-build-coordinate unit))
         ($inline ,@l)))

(define (build-with w x)
  (if (tm-func? x 'with)
      `(with ,@w ,@(cdr x))
      `(with ,@w ,x)))

(tm-define (markup-build-graphics-items l)
  (cond ((null? l) (list))
        ((tm-func? (car l) 'concat)
         (append (markup-build-graphics-items (cdar l))
                 (markup-build-graphics-items (cdr l))))
        ((tm-func? (car l) 'with)
         (let* ((head (cDr (cdar l)))
                (tail (cAr (car l)))
                (sl (if (tm-func? tail 'concat) (cdr tail) (list tail)))
                (sr (map markup-build-graphics-items sl))
                (sr* (map (lambda (x) (build-with head x)) sr))
                (r (markup-build-graphics-items (cdr l))))
           (append sr* r)))
        (else (cons (car l) (markup-build-graphics-items (cdr l))))))

(tm-define (markup-build-graphics l)
  (with x (append-map markup-expand-document l)
    (cons 'graphics (markup-build-graphics-items x))))

(tm-define-macro ($graphics . l)
  `(markup-build-graphics ($list ,@l)))

(tm-define (markup-build-coordinate x)
  (cond ((string? x) x)
        ((number? x)
         (if (exact? x)
             (number->string (exact->inexact x))
             (number->string x)))
        (else "0")))

(tm-define (markup-build-point l)
  (with x (append-map markup-expand-document l)
    (cons 'point (map markup-build-coordinate x))))

(tm-define-macro ($point . l)
  `(markup-build-point ($list ,@l)))
 
(tm-define-macro ($line . l)
  `(cons 'line ($list ,@l)))

(tm-define-macro ($cline . l)
  `(cons 'cline ($list ,@l)))

(tm-define-macro ($spline . l)
  `(cons 'spline ($list ,@l)))

(tm-define-macro ($cspline . l)
  `(cons 'cspline ($list ,@l)))

(tm-define-macro ($arc . l)
  `(cons 'arc ($list ,@l)))

(tm-define-macro ($carc . l)
  `(cons 'carc ($list ,@l)))

(tm-define-macro ($text-at p . l)
  ($quote `(text-at ($unquote ($inline ,@l)) ($unquote ($inline ,p)))))

(tm-define-macro ($math-at p . l)
  ($quote `(math-at ($unquote ($inline ,@l)) ($unquote ($inline ,p)))))

(tm-define (markup-build-graphical l)
  (with x (append-map markup-expand-document l)
    (if (== (length x) 1) (car x)
        (cons 'gr-group x))))

(tm-define-macro ($graphical . l)
  `(markup-build-graphical ($list ,@l)))

(tm-define-macro ($line-width w . l)
  ($quote `(with "line-width" ,w ($unquote ($graphical ,@l)))))

(tm-define-macro ($pen-color col . l)
  ($quote `(with "color" ,col ($unquote ($graphical ,@l)))))

(tm-define-macro ($fill-color col . l)
  ($quote `(with "fill-color" ,col ($unquote ($graphical ,@l)))))

(tm-define-macro ($text-align h v . l)
  ($quote `(with "text-at-halign" ,h
                 "text-at-valign" ,v
                 ($unquote ($inline ,@l)))))

(tm-define-macro ($graph2d x1 x2 steps fun)
  `($line
     ($for (_k_ (.. 0 (+ ,steps 1)))
       ($let* ((f ,fun)
               (dx (/ (- ,x2 ,x1) ,steps))
               (x (+ ,x1 (* _k_ dx)))
               (y (f x)))
         ($point x y)))))

(tm-define-macro ($curve2d t1 t2 steps xt yt)
  `($line
     ($for (_k_ (.. 0 (+ ,steps 1)))
       ($let* ((fx ,xt)
               (fy ,yt)
               (dt (/ (- ,t2 ,t1) ,steps))
               (t (+ ,t1 (* _k_ dt)))
               (x (fx t))
               (y (fy t)))
         ($point x y)))))



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; User interface for dynamic content generation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define-macro (tm-generate head . l)
  (receive (opts body) (list-break l not-define-option?)
    `(tm-define ,head ,@opts ($begin ,@body))))
