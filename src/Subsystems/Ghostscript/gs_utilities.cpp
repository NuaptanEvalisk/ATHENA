
/******************************************************************************
* MODULE     : gs_utilities.cpp
* DESCRIPTION: Ghostscript import of PostScript and EPS images only
* COPYRIGHT  : (C) 2010-2012 David Michel, Joris van der Hoeven, Denis Raux
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "tm_configure.hpp"
#ifdef USE_GS

#include "gs_utilities.hpp"
#include "analyze.hpp"
#include "file.hpp"
#include "image_files.hpp"

string
gs_system () {
#ifdef OS_MINGW
  url gs= url_system ("C:\\") * url_wildcard ("Program Files*") * url_system ("gs") * url_wildcard ("gs*")* url_system ("bin") * url_wildcard ("gswin*c.exe");
  gs = resolve (gs, "fr");
  if (!(is_rooted (gs) || is_here (gs) || is_parent (gs))) {
    return "xxx";
  }
  return concretize (gs);
#else
   return "gs";
#endif
}

#ifdef GS_EXE
string
gs_embedded () {
  string cmd; // no need to resolve each time
  
  url tmp= url_system (get_env ("ATHENA_PATH"));
  url gs= tmp * url_system (GS_EXE);
   
  if (exists (gs)) {
    cmd= concretize (gs);
  } else {
    cmd= gs_system ();
  }
  return cmd;
}
#endif

static string
gs_executable () {
#ifdef GS_EXE
  static string cmd;
  if(N (cmd) == 0) cmd= gs_embedded (); // init had to be postponed because of ATHENA_PATH initialization
#else
  static string cmd= gs_system ();
#endif
  return cmd;
}

bool
has_gs () {
  return exists_in_path (gs_executable ());
}

string
gs_prefix () {
  return string ("\"") * gs_executable () * string ("\"") * string (" ");
}

 // eps2write available starting with gs  9.14 (2014-03-26)
 // epswrite removed in gs 9.16 (2015-03-30)
string
eps_device () {
  static string dev; // no need to resolve each time
  if (dev == "") {
    string cmd= gs_prefix ()*" --version";
    string buf= var_eval_system (cmd);
    double ver;
    int pos=0;
    if (read_double (buf, pos, ver)) {
      if (DEBUG_CONVERT) debug_convert << "gs version :"<<buf<<LF;
      if (ver >= 9.14) dev="eps2write";
      else dev="epswrite";
    }
    else  convert_error << "Cannot determine gs version"<<LF;
  }
  return copy(dev);
}


bool
gs_supports (url image) {
  string s= suffix (image);
  if (s == "ps" || s == "eps") return true;
  return false;
}

void
gs_image_size (url image, int& w_pt, int& h_pt) {
  if (!gs_supports (image)) { w_pt= h_pt= 0; return; }
  bool ok;
  {
    if (DEBUG_CONVERT) debug_convert << "gs eps image size :"<<LF;
    int x1,y1,x2,y2;
    string buf;
    ok= !load_string (image, buf, false);
    if (ok) {
      //try finding Bounding box in file:
      ok= ps_read_bbox (buf, x1, y1, x2, y2);
      if (!ok) {
        // bbox not found ask gs to compute one :
        string cmd= gs_prefix ();
        cmd << "-dQUIET -dNOPAUSE -dBATCH -dSAFER -sDEVICE=bbox ";
        //Note: bbox device does a "smart" job of finding the cropbox on its own removing blank margins (*even* on eps files)
        //this is ok if we are reading a ps page
        // real eps pages with proper bounding boxes have been recognized before this and will have their BoundingBox respected
        cmd << sys_concretize (image);
        buf= eval_system (cmd);
        if (DEBUG_CONVERT) debug_convert << "gs cmd :" << cmd << LF
          << "answer :" << buf ;
        ok= ps_read_bbox (buf, x1, y1, x2, y2);
      }
      if (ok) {
        w_pt= x2-x1;
        h_pt= y2-y1;
        set_imgbox_cache (image->t, w_pt, h_pt, x1, y1);
      }
    }
  }
  if (!ok) { 
  convert_error << "Cannot read image file '" << image << "'"
                << " in gs_image_size" << LF;
  w_pt= 0; h_pt= 0;
  }
}

void 
gs_fix_bbox (url eps, int x1, int y1, int x2, int y2) {
// used to restore appropriate bounding box of an eps file in case epswrite 
// spuriously changes it (see gs_to_eps)
  string outbuf, buf;
  int inx1, iny1, inx2, iny2;
  bool err = load_string (eps, buf, false);
  if (!err) {
    if (DEBUG_CONVERT) debug_convert<< "fix_bbox input bbox : ";
    if ( !ps_read_bbox (buf, inx1, iny1, inx2, iny2 ) ) 
      return; //bbox not found... should not occur
    if (inx1!=x1 || iny1!=y1 || inx2!=x2 || iny2!=y2) {
      int pos= search_forwards ("%%BoundingBox:", buf);
      pos += 14;
      outbuf << buf(0, pos)
        << " " << as_string(x1) << " " << as_string(y1) 
        << " " << as_string(x2) << " " << as_string(y2) << "\n";
      skip_line (buf, pos);
      if (read (buf, pos, "%%HiResBoundingBox:")) skip_line (buf, pos);
      outbuf << buf (pos, N(buf));
      save_string (eps, outbuf, true);
      if (DEBUG_CONVERT) 
        debug_convert<< "restored bbox : " << ps_read_bbox (outbuf, x1, y1, x2, y2 )<<LF;
    }  
    set_imgbox_cache (eps->t, x2-x1, y2-y1, x1, y1);
  } 
}

bool
gs_to_png (url image, url png, int w, int h) {
  if (!gs_supports (image)) return false;
  string cmd;
  if (DEBUG_CONVERT) debug_convert << "gs_to_png using gs"<<LF;
  cmd= gs_prefix ();
  cmd << "-dQUIET -dNOPAUSE -dBATCH -dSAFER ";
  cmd << "-sDEVICE=pngalpha -dGraphicsAlphaBits=4 -dTextAlphaBits=4 ";
  cmd << "-g" << as_string (w) << "x" << as_string (h) << " ";
  cmd << "-sOutputFile=" << sys_concretize (png) << " ";
  int bbw, bbh;
  int rw, rh;
  int bx1, by1, bx2, by2;
  ps_bounding_box (image, bx1, by1, bx2, by2);
  bbw= bx2-bx1;
  bbh= by2-by1;
  if (bbw <= 0 || bbh <= 0 || w <= 0 || h <= 0) return false;
  rw=(w*72)/bbw;
  rh=(h*72)/bbh;
  cmd << "-r" << as_string (rw) << "x" << as_string (rh) << " ";  
  
  if (DEBUG_CONVERT) debug_convert << "w="<<w<<" h="<<h<<LF
      << "bbw="<<bbw<<" bbh="<<bbh<<LF
      <<" res ="<<rw<<" * "<<rh <<LF;
  
  {
    //don't use -dEPSCrop which works incorrectly if (bx1 != 0 || by1 != 0)
    cmd << "-c \" "<< as_string (-bx1) << " "<< as_string (-by1) <<" translate gsave \"  -f "
            << sys_concretize (image) << " -c \" grestore \"";    
  }
  string ans= eval_system (cmd);
  if (DEBUG_CONVERT) debug_convert << cmd <<LF
    << "answer :" << ans << LF;
  if (!exists (png)) {
    convert_error << "gs_to_png failed for " << image <<LF;
    return false;
  }
  return true;
}

void
gs_to_eps (url image, url eps) {
  if (!gs_supports (image)) return;
  string cmd;
  int bx1, by1, bx2, by2; // bounding box
  if (DEBUG_CONVERT) debug_convert << "gs_to_eps"<<LF;
  cmd= gs_prefix ();
  cmd << "-dQUIET -dNOPAUSE -dBATCH -dSAFER ";
  cmd << "-sDEVICE=" << eps_device ();
  cmd << " -sOutputFile=" << sys_concretize (eps) << " ";
  {
    ps_bounding_box (image, bx1, by1, bx2, by2);
    cmd << " -dDEVICEWIDTHPOINTS=" << as_string (bx2-bx1)
      << " -dDEVICEHEIGHTPOINTS=" << as_string (by2-by1)<<" ";
    //don't use -dEPSCrop which works incorrectly if (bx1 != 0 || by1 != 0)
    cmd << "-c \" "<< as_string (-bx1) << " " << as_string (-by1) 
      << " translate gsave \" "
      << sys_concretize (image)
      << " -c \" grestore \"";     
  }
  string ans= eval_system (cmd);
  if (DEBUG_CONVERT) debug_convert << cmd <<LF
    << "answer :" << ans << LF
    << "eps generated? " << exists (eps) << LF;
  // eps(2)write and bbox devices do a "smart" job of finding the boundingbox on their own,
  // possibly changing the original margins/aspect ratio of the input image.
  // here were restore the original size.
  gs_fix_bbox (eps, 0, 0, bx2-bx1, by2-by1);
}

// This conversion is appropriate for eps images
// (originally implemented in pdf_image_rep::flush)
void  
gs_to_pdf (url image, url pdf, int w, int h) {
  if (!gs_supports (image)) return;
  string cmd;
  if (DEBUG_CONVERT) debug_convert << "(eps) gs_to_pdf"<<LF;
  // take care of properly handling the bounding box
  // the resulting pdf image will always start at 0,0.

  int bx1, by1, bx2, by2; // bounding box
  ps_bounding_box (image, bx1, by1, bx2, by2);
  if (bx2 <= bx1 || by2 <= by1) return;
  if (w <= 0) w= bx2-bx1;
  if (h <= 0) h= by2-by1;
  double scale_x = w/((double)(bx2-bx1));
  double scale_y = h/((double)(by2-by1));

  cmd= gs_prefix ();
  cmd << " -dQUIET -dNOPAUSE -dBATCH -dSAFER -sDEVICE=pdfwrite ";
  cmd << "-dAutoRotatePages=/None ";
  cmd << "-dCompatibilityLevel=1.7 ";
  cmd << " -sOutputFile=" << sys_concretize (pdf) << " ";
  cmd << " -c \" << /PageSize [ " << as_string (bx2-bx1) << " " << as_string (by2-by1)
    << " ] >> setpagedevice gsave  "
    << as_string (-bx1) << " " << as_string (-by1) << " translate "
    << as_string (scale_x) << " " << as_string (scale_y) << " scale \"";
  cmd << " -f " << sys_concretize (image);
  cmd << " -c \" grestore \"  ";
  // debug_convert << cmd << LF;
  system (cmd);
  if (DEBUG_CONVERT)
    debug_convert << cmd << LF << "pdf generated? " << exists (pdf) << LF;
}

#endif
