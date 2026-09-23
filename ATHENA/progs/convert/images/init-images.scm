
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; MODULE      : init-images.scm
;; DESCRIPTION : setup image converters
;; COPYRIGHT   : (C) 2003  Joris van der Hoeven
;;
;; This software falls under the GNU general public license version 3 or later.
;; It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
;; in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(texmacs-module (convert images init-images))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper functions for testing available converters
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(tm-define (has-convert?)
  (and (not (os-mingw?)) ;; avoid name collision wrt Windows native command
       (url-exists-in-path? "convert")))

(tm-define (has-pdftocairo?)
  (url-exists-in-path? "pdftocairo"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Helper functions for conversions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


(define (get-raster-resolution opts)
  (or (assoc-ref opts "texmacs->image:raster-resolution")
      (get-preference "texmacs->image:raster-resolution")))

(define (get-shell-command cmd)
  (if (not (os-mingw?)) cmd
      (escape-shell (url-concretize (url-resolve-in-path cmd)))))

(tm-define (native-image-file->pdf x opts)
  (let* ((dest (assoc-ref opts 'dest))
         (res (get-raster-resolution opts))
         (dpi (cond ((number? res) res)
                    ((string? res) (or (string->number res) 96))
                    (else 96))))
    (image->pdf-file x dest 0 0 dpi)
    (if (url-exists? dest) dest #f)))

(tm-define (rsvg-convert x opts)
  (let* ((dest (assoc-ref opts 'dest))
         (fm (url-format (url-concretize dest)))
         (res (get-raster-resolution opts))
	 (cmd (get-shell-command "rsvg-convert")))
    (system-2 (string-append cmd " -f " fm " -d " res " -o ") dest x)
    (if (url-exists? dest) dest #f)))

(tm-define (pdf-file->pdftocairo-raster x opts)
  (let* ((dest (assoc-ref opts 'dest))
         (fullname (url-concretize dest))
         (format (url-format fullname))
         (fm (if (== format "tif") "tiff" format))
         (transp (if (== fm "png") "-transp " ""))
         (suffix (url-suffix fullname))
         (name (string-drop-right fullname (+ 1 (string-length suffix))))
         (res (get-raster-resolution opts))
	 (cmd (get-shell-command "pdftocairo")))
    ;;(display (string-append cmd " -singlefile " transp "-" fm " -r " res " " x " "  name))
    (system-2 (string-append cmd " -singlefile " transp "-" fm " -r " res)
	      x name)
    (if (url-exists? dest) dest #f)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Graphical document and geometric image formats
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-format postscript
  (:name "Postscript")
  (:suffix "ps" "eps"))

(define-format pdf
  (:name "Pdf")
  (:suffix "pdf"))

(converter pdf-file postscript-file
  (:require (url-exists-in-path? "pdftops"))
  (:shell "pdftops" "-eps" from to))

(converter postscript-file pdf-file
  (:function-with-options native-image-file->pdf))

(define-format xmgrace
  (:name "Xmgrace")
  (:suffix "agr" "xmgr"))

(converter xmgrace-file postscript-file
  (:require (url-exists-in-path? "xmgrace"))
  (:shell "xmgrace" "-noask -hardcopy -hdevice EPS -printfile" to from))

(define-format svg
   (:name "Svg")
   (:suffix "svg"))

(converter svg-file postscript-file
  (:require (url-exists-in-path? "inkscape"))
  (:shell "inkscape" "-z" "-f" from "-P" to))

(converter svg-file pdf-file
  (:require (url-exists-in-path? "inkscape"))
  (:shell "inkscape" "-z" "-f" from "-A" to))

(converter svg-file png-file
  (:require (url-exists-in-path? "inkscape"))
  (:shell "inkscape" "-z" "-d" "600" from "--export-png" to))

(converter svg-file png-file
  (:require (and (url-exists-in-path? "rsvg-convert")
                 (not (url-exists-in-path? "inkscape"))))
    (:function-with-options rsvg-convert))

(define-format geogebra
  (:name "Geogebra")
  (:suffix "ggb"))

(converter geogebra-file postscript-file
  (:require (url-exists-in-path? "geogebra"))
  (:shell "geogebra" "--export=" to "--dpi=600" from))

(converter geogebra-file svg-file
  (:require (url-exists-in-path? "geogebra"))
  (:shell "geogebra" "--export=" to "--dpi=600" from))

;;(converter pdf-file postscript-document
;;  (:require (has-pdftocairo?))
;;  (:shell "pdftocairo" "-eps" from to))
;;
;;(converter pdf-file postscript-file
;;  (:require (has-pdftocairo?))
;;  (:shell "pdftocairo" "-eps" from to))

(converter pdf-file svg-file
  (:require (has-pdftocairo?))
  (:shell "pdftocairo" "-origpagesizes -nocrop -nocenter -svg" from to))
  
(converter pdf-file svg-file
  (:require (url-exists-in-path? "pdf2svg"))
  (:shell "pdf2svg" from to))

(converter svg-file postscript-document
  (:require (qt6-gui?))
  (:function image->psdoc))
 
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Bitmap image formats
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define-format jpeg
  (:name "Jpeg")
  (:suffix "jpg" "jpeg"))

(converter jpeg-file postscript-document
  (:function image->psdoc))

(converter jpeg-file pnm-file
  (:require (has-convert?))
  (:shell "convert" from to))

(define-format tif
  (:name "Tif")
  (:suffix "tif" "tiff"))

(converter tif-file postscript-document
  (:function image->psdoc))
  
(converter tif-file png-file
  (:require (has-convert?))
  (:shell "convert" from to))

(define-format ppm
  (:name "Ppm")
  (:suffix "ppm"))

(converter ppm-file gif-file
  (:require (has-convert?))
  (:shell "convert" from to))

(define-format gif
  (:name "Gif")
  (:suffix "gif"))

(converter gif-file postscript-document
  (:function image->psdoc))

(converter gif-file pnm-file
  (:require (has-convert?))
  (:shell "convert" from to))

(define-format png
  (:name "Png")
  (:suffix "png"))

(converter png-file pdf-file
  (:require (qt6-gui?))
  (:function-with-options native-image-file->pdf))

(converter png-file postscript-document
  (:function image->psdoc))

(converter png-file pnm-file
  (:require (has-convert?))
  (:shell "convert" from to))

(converter geogebra-file png-file
  (:require (url-exists-in-path? "geogebra"))
  (:shell "geogebra" "--export=" to "--dpi=600" from))

(define-format pnm
  (:name "Pnm")
  (:suffix "pnm"))

(converter pnm-file postscript-document
  (:function image->psdoc))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; From vector graphics to bitmaps
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(converter pdf-file png-file
  (:require (has-pdftocairo?))
  (:function-with-options pdf-file->pdftocairo-raster)
  ;;(:option "texmacs->image:raster-resolution" "450")
  ;;if this is set it overrides the preference widget settings
  )

(converter pdf-file jpeg-file
  (:require (has-pdftocairo?))
  (:function-with-options pdf-file->pdftocairo-raster)
  ;;(:option "texmacs->image:raster-resolution" "300")
  )

(converter pdf-file tif-file
  (:require (has-pdftocairo?))
  (:function-with-options pdf-file->pdftocairo-raster))
