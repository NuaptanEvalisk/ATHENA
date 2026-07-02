#!/usr/bin/env bash
set -euo pipefail

prefix="${1:-${ATHENA_WIN64_PREFIX:-}}"
original_source_dir="${GUILE18_SOURCE_DIR:-/home/felix/data/Software/TeXmacs/obs/guile-1.8.8}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
source_dir="${GUILE18_STAGED_SOURCE_DIR:-${repo_root}/build_windows/src/guile-1.8.8-win64}"
build_dir="${GUILE18_BUILD_DIR:-${repo_root}/build_windows/deps-build/guile-1.8.8}"

if [[ -z "${prefix}" ]]; then
  echo "usage: $0 /path/to/x86_64-w64-mingw32-prefix" >&2
  exit 2
fi

if [[ ! -x "${original_source_dir}/configure" ]]; then
  echo "missing Guile 1.8 configure script: ${original_source_dir}/configure" >&2
  exit 2
fi

rm -rf "${source_dir}" "${build_dir}"
mkdir -p "$(dirname -- "${source_dir}")" "${build_dir}" "${prefix}"
cp -a "${original_source_dir}/." "${source_dir}/"
rm -f "${source_dir}/config.status" "${source_dir}/config.log"
find "${source_dir}" -name Makefile -delete
find "${source_dir}" \( -name '*.o' -o -name '*.lo' -o -name '*.la' -o -name '*.lai' \) -delete
perl -0pi -e 's/if test "\$ac_cv_sizeof_long" -ne "\$ac_cv_sizeof_void_p"; then\n\s*as_fn_error \$\? "sizes of long and void\* are not identical" "\$LINENO" 5\nfi/if test "\$ac_cv_sizeof_long" -ne "\$ac_cv_sizeof_void_p"; then\n  { printf "%s\\n" "\$as_me:\${as_lineno-\$LINENO}: WARNING: allowing LLP64 target where sizeof(long) != sizeof(void*)" >&5\nprintf "%s\\n" "\$as_me: WARNING: allowing LLP64 target where sizeof(long) != sizeof(void*)" >&2;}\nfi/s' "${source_dir}/configure"
perl -0pi -e 's/if test "\$cross_compiling" = yes\nthen :\n\s*\{ \{ printf "%s\\n" "\$as_me:\$\{as_lineno-\$LINENO\}: error: in .*?\nelse \$as_nop/if test "\$cross_compiling" = yes\nthen :\n  works=no\nelse \$as_nop/s' "${source_dir}/configure"
perl -0pi -e 's/#ifdef __MINGW32__\n# define SCM_IMPORT 1\n#endif\n//' "${source_dir}/libguile/guile.c"
perl -0pi -e 's/\Q#define SCM_GC_CARD_SIZE_MASK  (SCM_GC_SIZEOF_CARD-1)\E/#define SCM_GC_CARD_SIZE_MASK  ((scm_t_bits) (SCM_GC_SIZEOF_CARD - 1))/' "${source_dir}/libguile/gc.h"
perl -0pi -e 's/\Q#define SCM_GC_CELL_CARD(x)    ((scm_t_cell *) ((long) (x) & SCM_GC_CARD_ADDR_MASK))\E/#define SCM_GC_CELL_CARD(x)    ((scm_t_cell *) ((scm_t_bits) (x) & SCM_GC_CARD_ADDR_MASK))/' "${source_dir}/libguile/gc.h"
perl -0pi -e 's/\Q#define SCM_GC_CELL_OFFSET(x)  (((long) (x) & SCM_GC_CARD_SIZE_MASK) >> SCM_CELL_SIZE_SHIFT)\E/#define SCM_GC_CELL_OFFSET(x)  (((scm_t_bits) (x) & SCM_GC_CARD_SIZE_MASK) >> SCM_CELL_SIZE_SHIFT)/' "${source_dir}/libguile/gc.h"
perl -0pi -e 's/\Q#define SCM_GC_GET_CARD_FLAGS(card) ((long) ((card)->word_1))\E/#define SCM_GC_GET_CARD_FLAGS(card) ((scm_t_bits) ((card)->word_1))/' "${source_dir}/libguile/gc.h"
perl -0pi -e 's/1L << \(shift\)/(scm_t_bits) 1 << (shift)/g; s/1L << \(pos & SCM_C_BVEC_POS_MASK\)/(scm_t_c_bvec_long) 1 << (pos \& SCM_C_BVEC_POS_MASK)/g' "${source_dir}/libguile/gc.h"
perl -0pi -e 's/typedef unsigned long scm_t_c_bvec_long;\n\n#if \(SCM_SIZEOF_UNSIGNED_LONG == 8\)\n#       define SCM_C_BVEC_LONG_BITS    64\n#       define SCM_C_BVEC_OFFSET_SHIFT 6\n#       define SCM_C_BVEC_POS_MASK     63\n#       define SCM_CELL_SIZE_SHIFT     4\n#else\n#       define SCM_C_BVEC_LONG_BITS    32\n#       define SCM_C_BVEC_OFFSET_SHIFT 5\n#       define SCM_C_BVEC_POS_MASK     31\n#       define SCM_CELL_SIZE_SHIFT     3\n#endif/typedef unsigned long scm_t_c_bvec_long;\n\n#if (SCM_SIZEOF_UNSIGNED_LONG == 8)\n#       define SCM_C_BVEC_LONG_BITS    64\n#       define SCM_C_BVEC_OFFSET_SHIFT 6\n#       define SCM_C_BVEC_POS_MASK     63\n#else\n#       define SCM_C_BVEC_LONG_BITS    32\n#       define SCM_C_BVEC_OFFSET_SHIFT 5\n#       define SCM_C_BVEC_POS_MASK     31\n#endif\n#if (SCM_SIZEOF_UINTPTR_T == 8)\n#       define SCM_CELL_SIZE_SHIFT     4\n#elif (SCM_SIZEOF_UINTPTR_T == 4)\n#       define SCM_CELL_SIZE_SHIFT     3\n#else\n#       error unsupported Guile cell width\n#endif/' "${source_dir}/libguile/gc.h"
if ! grep -Fq 'SCM_SIZEOF_UINTPTR_T == 8' "${source_dir}/libguile/gc.h"; then
  echo "failed to decouple Guile GC cell size from unsigned long for Win64" >&2
  exit 1
fi
if grep -Fq '((long) (x)' "${source_dir}/libguile/gc.h"; then
  echo "failed to patch Guile GC card pointer casts for Win64" >&2
  exit 1
fi
perl -0pi -e 's/SCM key = scm_from_ulong \(\(unsigned long\) p\);/SCM key = scm_from_uintmax ((scm_t_uintmax) (scm_t_bits) p);/g' "${source_dir}/libguile/gc.c"
perl -0pi -e 's/SCM \*p = \(SCM \*\) \(scm_to_ulong \(SCM_CAAR \(l\)\)\);/SCM *p = (SCM *) (scm_t_bits) scm_to_uintmax (SCM_CAAR (l));/' "${source_dir}/libguile/gc-mark.c"
if grep -Eq 'scm_from_ulong \(\(unsigned long\) p\)|scm_to_ulong \(SCM_CAAR \(l\)\)' \
   "${source_dir}/libguile/gc.c" "${source_dir}/libguile/gc-mark.c"; then
  echo "failed to patch Guile registered-root pointer conversions for Win64" >&2
  exit 1
fi
perl -0pi -e 's/int revealed;/scm_t_bits revealed;/;
              s/unsigned long writingp;/scm_t_bits writingp;/;
              s/unsigned long fancyp;/scm_t_bits fancyp;/;
              s/unsigned long level;/scm_t_bits level;/;
              s/unsigned long length;/scm_t_bits length;/;
              s/unsigned long list_offset;/scm_t_bits list_offset;/;
              s/unsigned long top;/scm_t_bits top;/;
              s/unsigned long ceiling;/scm_t_bits ceiling;/' \
  "${source_dir}/libguile/print.h"
if grep -Eq 'int revealed;|unsigned long (writingp|fancyp|level|length|list_offset|top|ceiling);' \
   "${source_dir}/libguile/print.h"; then
  echo "failed to patch Guile print-state layout for Win64 LLP64" >&2
  exit 1
fi
perl -0pi -e 's/if \(SCM_I_INUMP \(n\)\)\n    \{\n      unsigned long m = \(unsigned long\) SCM_I_INUM \(n\);\n      SCM_ASSERT_RANGE \(1, n, SCM_I_INUM \(n\) > 0\);\n#if SCM_SIZEOF_UNSIGNED_LONG <= 4\n      return scm_from_uint32 \(scm_c_random \(SCM_RSTATE \(state\),\n                                            \(scm_t_uint32\) m\)\);\n#elif SCM_SIZEOF_UNSIGNED_LONG <= 8\n      return scm_from_uint64 \(scm_c_random64 \(SCM_RSTATE \(state\),\n                                              \(scm_t_uint64\) m\)\);\n#else\n#error "Cannot deal with this platform'"'"'s unsigned long size"\n#endif\n    \}/if (SCM_I_INUMP (n))\n    {\n      scm_t_signed_bits m = SCM_I_INUM (n);\n      SCM_ASSERT_RANGE (1, n, m > 0);\n#if SCM_SIZEOF_UINTPTR_T <= 4\n      return scm_from_uint32 (scm_c_random (SCM_RSTATE (state),\n                                            (scm_t_uint32) m));\n#elif SCM_SIZEOF_UINTPTR_T <= 8\n      return scm_from_uint64 (scm_c_random64 (SCM_RSTATE (state),\n                                              (scm_t_uint64) m));\n#else\n#error "Cannot deal with this platform'"'"'s SCM word size"\n#endif\n    }/s' "${source_dir}/libguile/random.c"
if grep -Fq 'unsigned long m = (unsigned long) SCM_I_INUM (n);' \
   "${source_dir}/libguile/random.c"; then
  echo "failed to patch Guile random inum conversion for Win64" >&2
  exit 1
fi
perl -0pi -e 's/          long xx = SCM_I_INUM \(x\);\n          long yy = SCM_I_INUM \(y\);\n          long int z = xx \+ yy;\n          return SCM_FIXABLE \(z\) \? SCM_I_MAKINUM \(z\) : scm_i_long2big \(z\);/          scm_t_signed_bits xx = SCM_I_INUM (x);\n          scm_t_signed_bits yy = SCM_I_INUM (y);\n          scm_t_signed_bits z;\n          if ((yy > 0 && xx > SCM_T_SIGNED_BITS_MAX - yy)\n              || (yy < 0 && xx < SCM_T_SIGNED_BITS_MIN - yy))\n            scm_num_overflow (s_sum);\n          z = xx + yy;\n          return SCM_FIXABLE (z) ? SCM_I_MAKINUM (z) : scm_from_signed_integer (z);/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/	  long int xx = SCM_I_INUM \(x\);\n	  long int yy = SCM_I_INUM \(y\);\n	  long int z = xx - yy;\n	  if \(SCM_FIXABLE \(z\)\)\n	    return SCM_I_MAKINUM \(z\);\n	  else\n	    return scm_i_long2big \(z\);/	  scm_t_signed_bits xx = SCM_I_INUM (x);\n	  scm_t_signed_bits yy = SCM_I_INUM (y);\n	  scm_t_signed_bits z;\n	  if ((yy < 0 && xx > SCM_T_SIGNED_BITS_MAX + yy)\n	      || (yy > 0 && xx < SCM_T_SIGNED_BITS_MIN + yy))\n	    scm_num_overflow (s_difference);\n	  z = xx - yy;\n	  if (SCM_FIXABLE (z))\n	    return SCM_I_MAKINUM (z);\n	  else\n	    return scm_from_signed_integer (z);/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/      long xx;\n\n    intbig:\n      xx = SCM_I_INUM \(x\);/      scm_t_signed_bits xx;\n\n    intbig:\n      xx = SCM_I_INUM (x);/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/	  long yy = SCM_I_INUM \(y\);\n	  long kk = xx \* yy;\n	  SCM k = SCM_I_MAKINUM \(kk\);\n	  if \(\(kk == SCM_I_INUM \(k\)\) && \(kk \/ xx == yy\)\)\n	    return k;\n	  else\n	    \{\n	      SCM result = scm_i_long2big \(xx\);\n	      mpz_mul_si \(SCM_I_BIG_MPZ \(result\), SCM_I_BIG_MPZ \(result\), yy\);\n	      return scm_i_normbig \(result\);\n	    \}/	  scm_t_signed_bits yy = SCM_I_INUM (y);\n#if defined(__GNUC__)\n	  __int128 kk = (__int128) xx * (__int128) yy;\n	  if (kk >= SCM_MOST_NEGATIVE_FIXNUM && kk <= SCM_MOST_POSITIVE_FIXNUM)\n	    return SCM_I_MAKINUM ((scm_t_signed_bits) kk);\n	  else if (kk >= SCM_T_INTMAX_MIN && kk <= SCM_T_INTMAX_MAX)\n	    return scm_from_signed_integer ((scm_t_intmax) kk);\n	  else\n	    scm_num_overflow (s_product);\n#else\n	  scm_t_signed_bits kk = xx * yy;\n	  SCM k = SCM_I_MAKINUM (kk);\n	  if ((kk == SCM_I_INUM (k)) && (kk \/ xx == yy))\n	    return k;\n	  else\n	    return scm_from_signed_integer (kk);\n#endif/s' "${source_dir}/libguile/numbers.c"
if grep -Eq 'long int z = xx \+ yy;|long int z = xx - yy;|long kk = xx \* yy;' \
   "${source_dir}/libguile/numbers.c"; then
  echo "failed to patch Guile fixnum arithmetic for Win64 LLP64" >&2
  exit 1
fi
perl -0pi -e 's/      long xx = SCM_I_INUM \(x\);\n      if \(SCM_I_INUMP \(y\)\)\n\t\{\n\t  long yy = SCM_I_INUM \(y\);\n\t  return scm_from_bool \(xx == yy\);\n\t\}/      scm_t_signed_bits xx = SCM_I_INUM (x);\n      if (SCM_I_INUMP (y))\n\t{\n\t  scm_t_signed_bits yy = SCM_I_INUM (y);\n\t  return scm_from_bool (xx == yy);\n\t}/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/          long yy = SCM_I_INUM \(y\);\n          return scm_from_bool \(xx == \(double\) yy\n\t\t\t\t&& \(DBL_MANT_DIG >= SCM_I_FIXNUM_BIT-1\n\t\t\t\t    \\|\\| \(long\) xx == yy\)\);/          scm_t_signed_bits yy = SCM_I_INUM (y);\n          return scm_from_bool (xx == (double) yy\n\t\t\t\t&& (DBL_MANT_DIG >= SCM_I_FIXNUM_BIT-1\n\t\t\t\t    || (scm_t_signed_bits) xx == yy));/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/      long xx = SCM_I_INUM \(x\);\n      if \(SCM_I_INUMP \(y\)\)\n\t\{\n\t  long yy = SCM_I_INUM \(y\);\n\t  return scm_from_bool \(xx < yy\);\n\t\}/      scm_t_signed_bits xx = SCM_I_INUM (x);\n      if (SCM_I_INUMP (y))\n\t{\n\t  scm_t_signed_bits yy = SCM_I_INUM (y);\n\t  return scm_from_bool (xx < yy);\n\t}/s' "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/\blong int xx = SCM_I_INUM/scm_t_signed_bits xx = SCM_I_INUM/g; s/\blong int yy = SCM_I_INUM/scm_t_signed_bits yy = SCM_I_INUM/g; s/\blong xx = SCM_I_INUM/scm_t_signed_bits xx = SCM_I_INUM/g; s/\blong yy = SCM_I_INUM/scm_t_signed_bits yy = SCM_I_INUM/g; s/\|\| xx == \(long\) yy/|| xx == (scm_t_signed_bits) yy/g; s/\|\| \(long\) xx == yy/|| (scm_t_signed_bits) xx == yy/g' "${source_dir}/libguile/numbers.c"
if grep -Eq '\blong( int)? (xx|yy) = SCM_I_INUM|\(long\) (xx|yy)' \
   "${source_dir}/libguile/numbers.c"; then
  echo "failed to patch Guile fixnum comparisons for Win64 LLP64" >&2
  exit 1
fi
perl -0pi -e 's/return scm_i_long2big \(-xx\);/return scm_from_signed_integer (-xx);/g' \
  "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/      long  x = SCM_I_INUM \(numerator\);/      scm_t_signed_bits x = SCM_I_INUM (numerator);/;
              s/\n\t  long y;\n\t  y = SCM_I_INUM \(denominator\);/\n\t  scm_t_signed_bits y;\n\t  y = SCM_I_INUM (denominator);/;
              s/long z = xx \/ yy;\n\s*if \(SCM_FIXABLE \(z\)\)\n\s*return SCM_I_MAKINUM \(z\);\n\s*else\n\s*return scm_i_long2big \(z\);/scm_t_signed_bits z = xx \/ yy;\n\t      if (SCM_FIXABLE (z))\n\t\treturn SCM_I_MAKINUM (z);\n\t      else\n\t\treturn scm_from_signed_integer (z);/g;
              s/long z = SCM_I_INUM \(x\) % yy;/scm_t_signed_bits z = SCM_I_INUM (x) % yy;/g;
              s/long z = xx % yy;\n\s*long result;/scm_t_signed_bits z = xx % yy;\n\t      scm_t_signed_bits result;/g;
              s/            long nn1 = SCM_I_INUM \(n1\);/            scm_t_signed_bits nn1 = SCM_I_INUM (n1);/' \
  "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/          long u = xx < 0 \? -xx : xx;\n          long v = yy < 0 \? -yy : yy;\n          long result;\n.*?          return \(SCM_POSFIXABLE \(result\)\n\s*\? SCM_I_MAKINUM \(result\)\n\s*: scm_i_long2big \(result\)\);/          scm_t_bits u = (xx < 0\n                          ? ((scm_t_bits) (-(xx + 1)) + 1)\n                          : (scm_t_bits) xx);\n          scm_t_bits v = (yy < 0\n                          ? ((scm_t_bits) (-(yy + 1)) + 1)\n                          : (scm_t_bits) yy);\n          while (v != 0)\n            {\n              scm_t_bits r = u % v;\n              u = v;\n              v = r;\n            }\n          return (SCM_POSFIXABLE (u)\n                  ? SCM_I_MAKINUM ((scm_t_signed_bits) u)\n                  : scm_from_unsigned_integer ((scm_t_uintmax) u));/s' \
  "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/          unsigned long result;\n          long yy;\n        big_inum:\n.*?          return \(SCM_POSFIXABLE \(result\)\s*\? SCM_I_MAKINUM \(result\)\s*: scm_from_ulong \(result\)\);/          scm_t_signed_bits yy;\n        big_inum:\n          yy = SCM_I_INUM (y);\n          if (yy == 0)\n            return scm_abs (x);\n          {\n            SCM result = scm_i_mkbig ();\n            scm_t_bits uy = (yy < 0\n                             ? ((scm_t_bits) (-(yy + 1)) + 1)\n                             : (scm_t_bits) yy);\n            mpz_t yy_z;\n            mpz_init (yy_z);\n            mpz_import (yy_z, 1, 1, sizeof (uy), 0, 0, &uy);\n            mpz_gcd (SCM_I_BIG_MPZ (result), SCM_I_BIG_MPZ (x), yy_z);\n            mpz_clear (yy_z);\n            scm_remember_upto_here_1 (x);\n            return scm_i_normbig (result);\n          }/s' \
  "${source_dir}/libguile/numbers.c"
perl -0pi -e 's/            SCM result = scm_i_mkbig \(\);\n            scm_t_signed_bits nn1 = SCM_I_INUM \(n1\);\n            if \(nn1 == 0\) return SCM_INUM0;\n            if \(nn1 < 0\) nn1 = - nn1;\n            mpz_lcm_ui \(SCM_I_BIG_MPZ \(result\), SCM_I_BIG_MPZ \(n2\), nn1\);\n            scm_remember_upto_here_1 \(n2\);\n            return result;/            SCM result = scm_i_mkbig ();\n            scm_t_signed_bits nn1 = SCM_I_INUM (n1);\n            scm_t_bits un1;\n            mpz_t nn1_z;\n            if (nn1 == 0) return SCM_INUM0;\n            un1 = (nn1 < 0\n                   ? ((scm_t_bits) (-(nn1 + 1)) + 1)\n                   : (scm_t_bits) nn1);\n            mpz_init (nn1_z);\n            mpz_import (nn1_z, 1, 1, sizeof (un1), 0, 0, &un1);\n            mpz_lcm (SCM_I_BIG_MPZ (result), SCM_I_BIG_MPZ (n2), nn1_z);\n            mpz_clear (nn1_z);\n            scm_remember_upto_here_1 (n2);\n            return scm_i_normbig (result);/s' \
  "${source_dir}/libguile/numbers.c"
if grep -Eq 'long  x = SCM_I_INUM|long y;|long z = (SCM_I_INUM \\(x\\)|xx) [%/] yy|long result;|long u = xx < 0|return scm_i_long2big \\(-xx\\)|long nn1 = SCM_I_INUM' \
   "${source_dir}/libguile/numbers.c"; then
  echo "failed to patch Guile core integer arithmetic for Win64 LLP64" >&2
  exit 1
fi
perl -0pi -e 's/symbols = scm_make_weak_key_hash_table \(scm_from_int \(2139\)\);/symbols = scm_c_make_hash_table (2139);/' "${source_dir}/libguile/symbols.c"
if grep -Fq 'symbols = scm_make_weak_key_hash_table (scm_from_int (2139));' \
   "${source_dir}/libguile/symbols.c"; then
  echo "failed to patch Guile symbol obarray away from weak hash table for Win64" >&2
  exit 1
fi

prefix="$(cd -- "${prefix}" && pwd)"
source_dir="$(cd -- "${source_dir}" && pwd)"
build_dir="$(cd -- "${build_dir}" && pwd)"

export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig:${prefix}/lib64/pkgconfig:${prefix}/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${prefix}"
export PKG_CONFIG_PATH=

export CC=x86_64-w64-mingw32-gcc
export CXX=x86_64-w64-mingw32-g++
export AR=x86_64-w64-mingw32-ar
export RANLIB=x86_64-w64-mingw32-ranlib
export STRIP=x86_64-w64-mingw32-strip
export WINDRES=x86_64-w64-mingw32-windres

cppflags=("-I${prefix}/include")
ldflags=("-L${prefix}/lib" "-L${prefix}/lib64")

cd "${build_dir}"

"${source_dir}/configure" \
  --host=x86_64-w64-mingw32 \
  --build="$("${source_dir}/build-aux/config.guess")" \
  --prefix="${prefix}" \
  --enable-static \
  --disable-shared \
  --disable-error-on-warning \
  --with-threads=null \
  CPPFLAGS="${cppflags[*]}" \
  LDFLAGS="${ldflags[*]}"

scmconfig_candidate="${build_dir}/libguile/gen-scmconfig.h"
if [[ -f "${scmconfig_candidate}" ]]; then
  grep -Eq '^#define SCM_SIZEOF_UNSIGNED_LONG 4$' "${scmconfig_candidate}" || \
    echo "warning: expected Win64 LLP64 unsigned long size in ${scmconfig_candidate}" >&2
  grep -Eq '^#define SCM_SIZEOF_UINTPTR_T 8$' "${scmconfig_candidate}" || \
    echo "warning: expected Win64 uintptr_t size in ${scmconfig_candidate}" >&2
fi

make -j1
make install

if [[ -d "${prefix}/lib64" ]]; then
  mkdir -p "${prefix}/lib"
  for lib in "${prefix}"/lib64/libguile*.a "${prefix}"/lib64/libguile*.la; do
    [[ -e "${lib}" ]] || continue
    ln -sf "../lib64/$(basename -- "${lib}")" "${prefix}/lib/$(basename -- "${lib}")"
  done
fi

for pc in "${prefix}"/lib/pkgconfig/guile-1.8.pc "${prefix}"/lib64/pkgconfig/guile-1.8.pc; do
  [[ -f "${pc}" ]] || continue
  if [[ -e "${prefix}/lib/libintl.dll.a" || -e "${prefix}/lib/libintl.a" ]] &&
     ! grep -Eq '(^|[[:space:]])-lintl($|[[:space:]])' "${pc}"; then
    perl -0pi -e 's/^(Libs:.*)$/$1 -lintl/m' "${pc}"
  fi
done

PKG_CONFIG_LIBDIR="${PKG_CONFIG_LIBDIR}" \
PKG_CONFIG_SYSROOT_DIR="${PKG_CONFIG_SYSROOT_DIR}" \
pkg-config --modversion guile-1.8
