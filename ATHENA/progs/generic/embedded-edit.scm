
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : embedded-edit.scm
;; DESCRIPTION : routines for managing embedded and linked images
;; COPYRIGHT   : (C) 2018  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic embedded-edit)
  (:use (utils library tree)
        (generic generic-edit)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Image contexts
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Manage embedded images
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (save-embedded-image-as)
  (:interactive #t)
  (let* ((t (tree-innermost embedded-image-context? #t))
         (s (embedded-suffix t))
         (p (embedded-propose t 1)))
    (choose-file embedded-saver "Save embedded image" s "Save" p)))

(tm-define (link-embedded-image-as)
  (:interactive #t)
  (let* ((t (tree-innermost embedded-image-context? #t))
         (s (embedded-suffix t))
         (p (embedded-propose t 1)))
    (choose-file embedded-linker "Link embedded image" s "Save" p)))

(tm-define (link-embedded-image-copies-as)
  (:interactive #t)
  (let* ((t (tree-innermost embedded-image-context? #t))
         (s (embedded-suffix t))
         (p (embedded-propose t 1)))
    (choose-file embedded-linker-copies "Link embedded image and copies"
                 s "Save" p)))

(define (strip-suffix u)
  (with suffix (url-suffix u)
    (if (== suffix "") u
        (with r (url-unglue u (+ (string-length suffix) 1))
          (if (string? u) (url->string r) r)))))

(define (url-number u nr)
  (with num (string-append "-" (number->string nr))
    (if (== (url-suffix u) "")
        (url-glue u num)
        (url-glue (strip-suffix u) (string-append num "." (url-suffix u))))))

(define (url-free u nr)
  (cond ((not (url-exists? u)) u)
        ((not (url-exists? (url-number u nr))) (url-number u nr))
        (else (url-free u (+ nr 1)))))

(define (embedded-list t)
  (let* ((tl (tree-search t embedded-image-context?))
         (il (... 1 (length tl)))
         (fl (map embedded-propose tl il)))
    (map list tl fl)))

(tm-define (save-all-embedded-images)
  (for (p (embedded-list (buffer-tree)))
    (with (t u) p
      (save-embedded-image t (url-free u 2)))))

(tm-define (link-all-embedded-images)
  (for (p (embedded-list (buffer-tree)))
    (with (t u) p
      (link-embedded-image t (url-free u 2)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Manage linked images
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (remove-image-background)
  (:interactive #t)
  (with t (tree-innermost image-context? #t)
    (if (not t)
        (show-message "No image selected." "Remove background")
        (if (embedded-image-context? t)
            (show-message "Remove background supports linked PNG images only."
                          "Remove background")
        (let* ((f (tm->string (tm-ref t 0)))
               (err (image-remove-background f)))
          (if (== err "")
              (begin
                (when (defined? 'picture-gc) (picture-gc))
                (set-message "Removed image background" "Remove background"))
              (show-message err "Remove background")))))))
