/******************************************************************************
* MODULE     : kpathsea_lookup.cpp
* DESCRIPTION: Isolated interface to libkpathsea
*******************************************************************************/

#include "kpathsea_lookup.hpp"

#ifdef USE_KPATHSEA_API
#include <kpathsea/kpathsea.h>

char*
athena_kpathsea_find (const char* name, athena_kpathsea_format format) {
  static bool initialized= false;
  if (!initialized) {
    kpse_set_program_name ("ATHENA", "ATHENA");
    initialized= true;
  }
  kpse_file_format_type kpse_format= kpse_tfm_format;
  if (format == ATHENA_KPSE_PK) kpse_format= kpse_pk_format;
  if (format == ATHENA_KPSE_TYPE1) kpse_format= kpse_type1_format;
  return kpse_find_file (name, kpse_format, true);
}
#endif
