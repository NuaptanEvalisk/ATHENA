
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : document-style.scm
;; DESCRIPTION : management of global document style
;; COPYRIGHT   : (C) 2001--2013  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (generic document-style))
(import-from (kernel athena tm-preferences))

(define (native-document-background-setter value)
  (init-env-tree "bg-color" value))

(tm-define (native-document-background-pattern-dialog)
  (open-pattern-selector native-document-background-setter "1cm"))

(tm-define (native-document-background-gradient-dialog)
  (open-gradient-selector native-document-background-setter))

(tm-define (native-document-background-picture-dialog)
  (open-background-picture-selector native-document-background-setter))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Relations between style files and packages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Menu names of style files and packages, and balloon help
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(comment "Style names, descriptions and category relationships live in
ATHENA/misc/styles/catalog.json and are queried by native C++.")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Getting and setting the list of style packages
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; High level routines for style and style package management
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-property (set-no-style)
  (:check-mark "v" has-no-style?))

(tm-define (set-main-style style)
  (:synopsis* "Set main document style")
  (:argument style "Style")
  (:default  style "generic")
  (:check-mark "v" has-main-style?)
  (:balloon style-get-documentation)
  (let* ((old (get-style-list))
         (new (if (null? old) (list style) (cons style (cdr old)))))
    (set-style-list new))
  (delayed
    (:idle 1)
    (notify-new-style style)))

(tm-define (install-custom-style name)
  (:synopsis* "Install custom document style")
  (let ((style-name (custom-style-file-name name)))
    (cond ((not (or (string-ends? (url->system (url-tail name)) ".ats")
                    (string-ends? (url->system (url-tail name)) ".ts")))
           (show-message "Please select an ATHENA .ats style or legacy .ts style."
                          "Install custom style"))
          ((not (install-custom-style-file name))
           (show-message "Could not install the selected style."
                         "Install custom style"))
          (else
            (set-main-style style-name)
            (show-message
             (string-append "Installed and activated style: " style-name)
            "Install custom style")))))

(tm-define (choose-and-install-custom-style)
  (:synopsis* "Choose and install custom document style")
  (choose-file install-custom-style "Install custom style" ""))

(tm-property (add-style-package pack)
  (:synopsis* "Add style package")
  (:argument pack "Package")
  (:check-mark "v" has-style-package?)
  (:balloon style-get-documentation))

(tm-property (remove-style-package pack)
  (:argument pack "Remove package")
  (:proposals pack (with l (get-style-list) (if (null? l) l (cdr l))))
  (:balloon style-get-documentation))

(tm-property (remove-style-package* pack)
  (:argument pack "Remove package")
  (:check-mark "v" not-has-style-package?))

(tm-property (toggle-style-package pack)
  (:argument pack "Toggle package")
  (:check-mark "v" has-style-package?)
  (:balloon style-get-documentation))

(tm-define (edit-package-source name)
  (with file-name (url-resolve-package name)
    (cursor-history-add (cursor-path))
    (load-document file-name)
    (cursor-history-add (cursor-path))))

(tm-define (edit-style-source)
  (with l (get-style-list)
    (when (and (nnull? l) (string? (car l)))
      (edit-package-source (car l)))))

(tm-define (make* l name)
  (if (or (has-style-package? name)
	  (style-has? (string-append name "-package")))
      (make l)
      (begin
	(add-style-package name)
	(delayed
	  (:idle 1)
	  (make l)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; UI-independent rules migrated from retired Scheme menu modules
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (get-user-preferred-fonts)
  (let ((p (get-preference "preferred fonts")))
    (if (== p "") '()
        (let ((obj (string->object p)))
          (if (list? obj) obj '())))))
