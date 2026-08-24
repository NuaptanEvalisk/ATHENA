
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : tm-vault-persons.scm
;; DESCRIPTION : semantic person-name normalization and navigation
;; COPYRIGHT   : (C) 2026  Nuaptan
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (athena athena tm-vault-persons))
(import-from (kernel athena tm-preferences))

(define (vault-person-normalization-supported? buf)
  (and (url? buf)
       (== (url-suffix buf) "ath")
       (buffer-exists? buf)))

(define (vault-person-normalize-buffer! buf)
  (let* ((body (buffer-get-body buf))
         (normalized (athena-normalize-person-names body)))
    (if (== body normalized)
        #f
        (begin
          (buffer-set-body buf normalized)
          #t))))

(tm-define (vault-person-before-save buf cont)
  (when (and (== (get-preference "vault normalize person names on save") "on")
             (vault-person-normalization-supported? buf))
    (vault-person-normalize-buffer! buf))
  (cont))

(tm-define (open-persons-explorer)
  (:interactive #t)
  (if (not (vault-active?))
      (show-message "No active vault. Please load a vault first."
                    "Persons Explorer")
      (persons-explorer-show)))
