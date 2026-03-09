/* src/System/config.h.  Generated from config.in by configure.  */
/* src/System/config.in.  Generated from configure.in by autoheader.  */

/* Qt major version number */
#define AC_QT_MAJOR_VERSION 5

/* The normal alignment of 'void *', in bytes. */
#define ALIGNOF_VOID_P 8

/* Enable experimental Cocoa port */
/* #undef AQUATEXMACS */

/* If there is a static plugin Cocoa */
/* #undef CocoaPlugin */

/* check assertions in code */
#define DEBUG_ASSERT 1

/* debugging built */
/* #undef DEBUG_ON */

/* Defined if ...-style argument passing works */
/* #undef DOTS_OK */

/* Enable experimental style rewriting code */
/* #undef EXPERIMENTAL */

/* gs path relative to ATHENA_PATH */
/* #undef GS_EXE */

/* gs fonts relative to ATHENA_PATH */
/* #undef GS_FONTS */

/* gs lib path relative to ATHENA_PATH */
/* #undef GS_LIB */

/* Guile version */
/* #undef GUILE_A */

/* Guile version */
/* #undef GUILE_B */

/* Guile version */
#define GUILE_C 1

/* Guile version */
/* #undef GUILE_D */

/* Guile 1.6 header */
/* #undef GUILE_HEADER_16 */

/* Guile 1.8 header */
/* #undef GUILE_HEADER_18 */

/* Guile version */
#define GUILE_VERSION 1.8

/* Define to 1 if the system has the type 'FILE'. */
#define HAVE_FILE 1

/* Define to 1 if you have the 'gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if the system has the type 'intptr_t'. */
#define HAVE_INTPTR_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <pty.h> header file. */
#define HAVE_PTY_H 1

/* Define if the Qt framework is available. */
#define HAVE_QT 1

/* Define to 1 if you have the 'snprintf' function. */
#define HAVE_SNPRINTF 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if the system has the type 'time_t'. */
#define HAVE_TIME_T 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <util.h> header file. */
/* #undef HAVE_UTIL_H */

/* Define to 1 if you have the <X11/Xlib.h> header file. */
/* #undef HAVE_X11_XLIB_H */

/* Define to 1 if you have the <X11/Xutil.h> header file. */
/* #undef HAVE_X11_XUTIL_H */

/* Link axel library with TeXmacs */
/* #undef LINKED_AXEL */

/* Link cairo library with TeXmacs */
/* #undef LINKED_CAIRO */

/* Freetype library available */
#define LINKED_FREETYPE 1

/* Link imlib2 library with TeXmacs */
/* #undef LINKED_IMLIB2 */

/* Link sqlite3 library with TeXmacs */
/* #undef LINKED_SQLITE3 */

/* Enabling Mac OSX extensions */
/* #undef MACOSX_EXTENSIONS */

/* makeappx location */
/* #undef MAKEAPPX */

/* makepri location */
/* #undef MAKEPRI */

/* Max fast alloc // WORD_LENGTH more than power of 2 */
#define MAX_FAST 264 

/* Disable fast memory allocator */
/* #undef NO_FAST_ALLOC */

/* Use g++ strictly prior to g++ 3.0 */
/* #undef OLD_GNU_COMPILER */

/* OS type */
/* #undef OS_ANDROID */

/* OS type */
/* #undef OS_CYGWIN */

/* OS type */
/* #undef OS_DARWIN */

/* OS type */
/* #undef OS_FREEBSD */

/* OS type */
#define OS_GNU_LINUX 1

/* OS type */
/* #undef OS_HAIKU */

/* OS type */
/* #undef OS_IRIX */

/* OS type */
/* #undef OS_MACOS */

/* OS type */
/* #undef OS_MINGW */

/* OS type */
/* #undef OS_MINGW64 */

/* OS type */
/* #undef OS_POWERPC_GNU_LINUX */

/* OS type */
/* #undef OS_SOLARIS */

/* OS type */
/* #undef OS_SUN */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* Disable DCT */
#define PDFHUMMUS_NO_DCT 1

/* Disable Tiff Format */
#define PDFHUMMUS_NO_TIFF 1

/* Enabling native PDF renderer */
#define PDF_RENDERER 1

/* Enabling Qt pipes */
#define QTPIPES 1

/* Enable experimental Qt port */
#define QTTEXMACS 1

/* sanitizers linked */
/* #undef SANITIZERS_ON */

/* Enable checks style rewriting code */
/* #undef SANITY_CHECKS */

/* signtool location */
/* #undef SIGNTOOL */

/* The size of 'void *', as computed by sizeof. */
#define SIZEOF_VOID_P 8

/* If not set during link */
#define STACK_SIZE 0x1000000

/* Define to 1 if all of the C89 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Special fix */
/* #undef ATHENA_FIX_1_GNUTLS */

/* Svn build revision */
#define ATHENA_REVISION "Custom Unversioned directory"

/* Dynamic linking function name */
#define TM_DYNAMIC_LINKING dlopen

/* Use aspell library */
/* #undef USE_ASPELL */

/* Use axel library */
/* #undef USE_AXEL */

/* Use cairo library */
/* #undef USE_CAIRO */

/* Use freetype library */
#define USE_FREETYPE 3

/* Use GnuTLS library version >= 3 */
/* #undef USE_GNUTLS */

/* Use ghostscript */
#define USE_GS 1

/* Use iconv library */
#define USE_ICONV 1

/* Use imlib2 library */
/* #undef USE_IMLIB2 */

/* Use intl library */
/* #undef USE_INTL */

/* Use Sparkle framework */
/* #undef USE_SPARKLE */

/* Use sqlite3 library */
/* #undef USE_SQLITE3 */

/* Use C++ stack backtraces */
#define USE_STACK_TRACE 1

/* Pointer size */
#define WORD_LENGTH 8

/* Pointer increment */
#define WORD_LENGTH_INC 7

/* Word Mask */
#define WORD_MASK 0xfffffffffffffff8

/* Use standard X11 port */
/* #undef X11TEXMACS */

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */

/* Guile string size type */
#define guile_str_size_t size_t

/* Qt without fontconfig */
/* #undef qt_no_fontconfig */

/* If there is a static plugin qgif */
/* #undef qt_static_plugin_qgif */

/* If there is a static plugin qico */
/* #undef qt_static_plugin_qico */

/* If there is a static plugin qjpeg */
/* #undef qt_static_plugin_qjpeg */

/* If there is a static plugin qsvg */
/* #undef qt_static_plugin_qsvg */

/* If Qt is statically linked */
/* #undef qt_static_plugin_xcb */
