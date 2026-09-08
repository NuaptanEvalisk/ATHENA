;;; documentation.scm --- Run-time documentation facility
;;; Copyright (C) 2000-2003,2006,2009,2010,2024 Free Software Foundation, Inc.
;;;
;;; This library is free software: you can redistribute it and/or modify
;;; it under the terms of the GNU Lesser General Public License as
;;; published by the Free Software Foundation, either version 3 of the
;;; License, or (at your option) any later version.
;;;
;;; This library is distributed in the hope that it will be useful, but
;;; WITHOUT ANY WARRANTY; without even the implied warranty of
;;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;;; Lesser General Public License for more details.
;;;
;;; You should have received a copy of the GNU Lesser General Public
;;; License along with this program.  If not, see
;;; <http://www.gnu.org/licenses/>.

;;; In-memory docstrings and source commentary; no generated documentation database.

(define-module (ice-9 documentation)
  #:use-module (ice-9 rdelim)
  #:use-module (ice-9 regex)
  #:use-module (ice-9 match)
  #:export (file-commentary
            object-documentation))


;;
;; commentary extraction
;;

(define (file-commentary filename . cust) ; (IN-LINE-RE AFTER-LINE-RE SCRUB)

  (define default-in-line-re (make-regexp "^;;; Commentary:"))
  (define default-after-line-re (make-regexp "^;;; Code:"))
  (define default-scrub (let ((dirt (make-regexp "^;+")))
			  (lambda (line)
			    (let ((m (regexp-exec dirt line)))
			      (if m (match:suffix m) line)))))
       
  ;; fixme: might be cleaner to use optargs here...
  (let ((in-line-re (if (> 1 (length cust))
                        default-in-line-re
                        (let ((v (car cust)))
                          (cond ((regexp? v) v)
                                ((string? v) (make-regexp v))
                                (else default-in-line-re)))))
        (after-line-re (if (> 2 (length cust))
                           default-after-line-re
                           (let ((v (cadr cust)))
                             (cond ((regexp? v) v)
                                   ((string? v) (make-regexp v))
                                   (else default-after-line-re)))))
        (scrub (if (> 3 (length cust))
                   default-scrub
                   (let ((v (caddr cust)))
                     (cond ((procedure? v) v)
                           (else default-scrub))))))
    (call-with-input-file filename
      (lambda (port)
	(let loop ((line (read-delimited "\n" port))
		   (doc "")
		   (parse-state 'before))
	  (if (or (eof-object? line) (eq? 'after parse-state))
	      doc
	      (let ((new-state
		     (cond ((regexp-exec in-line-re line) 'in)
			   ((regexp-exec after-line-re line) 'after)
			   (else parse-state))))
		(if (eq? 'after new-state)
		    doc
		    (loop (read-delimited "\n" port)
			  (if (and (eq? 'in new-state) (eq? 'in parse-state))
			      (string-append doc (scrub line) "\n")
			      doc)
			  new-state)))))))))



(define (object-documentation object)
  "Return the docstring for OBJECT.
OBJECT can be a procedure, macro or any object that has its
`documentation' property set."
  (or (and (procedure? object)
	   (procedure-documentation object))
      (object-property object 'documentation)
      (and (macro? object)
           (object-documentation (macro-transformer object)))))

;;; documentation.scm ends here
