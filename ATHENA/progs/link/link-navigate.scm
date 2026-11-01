
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : link-navigate.scm
;; DESCRIPTION : navigation routines for links
;; COPYRIGHT   : (C) 2006  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (link link-navigate)
  (:use (utils library cursor)
        (generic generic-edit)))
(import-from (kernel athena tm-preferences))

(define (unescape-link-args s)
  (string-replace s "%3A" ":"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Navigation lists contain concrete propositions for link traversal.
;; One item is of the form
;;   (type attrs n source-id target-vertex)
;; where type and attrs are the type and the attributes of the link,
;; source-id the source identifier and the target vertex target-vertex
;; the n-th vertex of the link. In particular, for simple links,
;; n=1 for direct traversal and n=0 for inverse traversal.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (navigation-type item) (car item))
(tm-define (navigation-attributes item) (cadr item))
(tm-define (navigation-pos item) (caddr item))
(tm-define (navigation-source item) (cadddr item))
(tm-define (navigation-target item) (car (cddddr item)))

(define (resolve-navigation-list l fun)
  (if (null? l) (fun)
      (let* ((id (vertex->id (navigation-target (car l))))
             (ok (or (nstring? id) (nnull? (id->trees id)))))
        (if ok (resolve-navigation-list (cdr l) fun)
            (begin
              (resolve-id id)
              (delayed (:idle 25) (resolve-navigation-list (cdr l) fun)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Link pages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (build-navigation-page-sub style l)
  (with doc (navigation-list->document style l)
    (open-auxiliary "Link page" doc)))

(tm-define (build-navigation-page l)
  (let* ((style `(tuple ,@(get-style-list)))
         (fun (lambda () (build-navigation-page-sub style l))))
    (resolve-navigation-list l fun)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Jumping to different types of URLs is done in two stages. The load handler,
;; changing with the type of root of the url and the post-load handler, which
;; will tipically depend on the file format.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; FIXME: many corner cases will get through 
;(inexistent relative paths for instance)
(define (default-filter-url u)
  (with (base qry) (process-url u)
    (or (== base "")
        (or (!= (url-root base) "default")
            (url-exists? base)))))

(define (default-root-disambiguator u after-open)
 (with l (list-filter (url->list u) default-filter-url)
    (cond ((null? l) 
           (set-message `(verbatim ,(url->system u)) "Not found"))
          ((== 1 (length l)) (load-browse-buffer (car l) after-open))
          (else (build-disambiguation-page l)))))

(define (process-url u)
  "Split a simple (not or'ed!!) url in base and query"
  (let* ((s (url->system u))
         (m (string-length s))
         (h (or (string-index s #\#) m))
         (a (or (string-index s #\?) m))
         (i (min m h a))
         (base (substring s 0 i))
         (qry (substring s (min m (+ 1 i)) m)))
    (if (< i m)  ; was there either a '?' or a '#' (with args)?
        (list (system->url (string-drop-right s (+ 1 (string-length qry))))
              (unescape-link-args qry))
        (list u ""))))

(define (texmacs-file-post qry)
  (if (and (string? qry) (not (string-null? qry)))
      (with dest (unescape-link-args qry)
        (go-to-label dest)
        (set-message (replace "At %1." dest) ""))))

(define generic-file-post texmacs-file-post)

(define (source-file-post qry)
  (let* ((lstr (or (query-ref qry "line") "0"))
         (cstr (or (query-ref qry "column") "0"))
         (sstr (or (query-ref qry "select") ""))
         (line (or (string->number lstr) 0))
         (column (or (string->number cstr) 0)))
    (go-to-line line)
    (go-to-column column)
    (set! column (or (select-word sstr (cursor-tree) column) 0))
    (set-message sstr (string-append lstr ":" (number->string column)))))

; FIXME? will this jump to HTML anchors?
(define html-file-post generic-file-post)

(define (default-post-handler u)
  (with (base qry) (process-url u)
    (with fm (file-format base)
      (cond ((== qry "") (noop))
            ((== fm "texmacs-file") (texmacs-file-post qry))
            ((== fm "generic-file") (generic-file-post qry))
            ((== fm "cpp-file") (source-file-post qry))
            ((== fm "java-file") (source-file-post qry))
            ((== fm "scala-file") (source-file-post qry))
            ((== fm "python-file") (source-file-post qry))
            ((== fm "julia-file") (source-file-post qry))
            ((== fm "html-file") (html-file-post qry))
            (else
              (display* "Unhandled format for default queries: " fm "\n"))))))

(define (http-post-handler u)
  ; TODO: jump to anchors in HTML
  (noop))

(define (default-root-handler u after-open)
  (cond ((and (url-rooted-protocol? u "default")
              (url-rooted-web? (current-buffer)))
         (default-root-handler (url-relative (current-buffer) u) after-open))
        ((url-or? (url-expand u))
         (default-root-disambiguator (url-expand u) after-open))
        (else (with (base qry) (process-url u)
                (if (!= "" (url->system base))
                    (load-browse-buffer base after-open))))))

(define (tmfs-root-handler u after-open)
  (load-browse-buffer u after-open))

(define (http-root-handler u after-open)
  (load-browse-buffer u after-open))

(define (url-handlers u)
  (with root (or (and (url-rooted? u) (url-root u)) "default")
    (cond ((== root "default") 
           (list default-root-handler default-post-handler))
          ((== root "tmfs") 
           (list tmfs-root-handler (lambda (x) (noop))))
          ((== root "file") ;; TODO: to be refined
           (list http-root-handler http-post-handler))
          ((or (== root "http") (== root "https") (== root "doi"))
           (list http-root-handler http-post-handler))
          ((== root "mailto")
           (list default-root-handler (lambda (x) (noop))))
          (else (display* "Unhandled url root: " root "\n")
                (list default-root-handler default-post-handler)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Actual navigation
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (go-to-id id . opt-from)
  (:synopsis "Jump to the first locus with a given identifier @id")
  (:argument opt-from "Optional path for the cursor history")
  (with l (id->trees id)
    (if (nnull? l)
        (begin
          (if (nnull? opt-from) (cursor-history-add (car opt-from)))
          (tree-go-to (car l) :end)
          (if (nnull? opt-from) (cursor-history-add (cursor-path))))
        (and (resolve-id id)
             (delayed (:idle 25) (apply go-to-id (cons id opt-from)))))))

(define (wikilink-url? u)
  (or (url-rooted-tmfs-protocol? u "wikilink")
      (url-rooted-tmfs-protocol? u "Wikilink")))

(tm-define (go-to-url u . opt-from)
  (:synopsis "Jump to the url @u")
  (:argument opt-from "Optional path for the cursor history")
  (if (nnull? opt-from) (cursor-history-add (car opt-from)))
  (if (string? u) (set! u (system->url u)))
  (with (action post) (url-handlers u)
    (let ((text (string-copy (url->system u)))
          (record-history? (and (nnull? opt-from) (not (wikilink-url? u)))))
      (action u (lambda ()
                  (post (system->url text))
                  (if record-history?
                      (cursor-history-add (cursor-path))))))))
