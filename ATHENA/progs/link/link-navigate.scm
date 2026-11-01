
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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Navigation lists contain concrete propositions for link traversal.
;; One item is of the form
;;   (type attrs n source-id target-vertex)
;; where type and attrs are the type and the attributes of the link,
;; source-id the source identifier and the target vertex target-vertex
;; the n-th vertex of the link. In particular, for simple links,
;; n=1 for direct traversal and n=0 for inverse traversal.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

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
