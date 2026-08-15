/******************************************************************************
* MODULE     : kpathsea_lookup.hpp
* DESCRIPTION: Isolated interface to libkpathsea
*******************************************************************************/

#ifndef KPATHSEA_LOOKUP_H
#define KPATHSEA_LOOKUP_H

enum athena_kpathsea_format {
  ATHENA_KPSE_TFM,
  ATHENA_KPSE_PK,
  ATHENA_KPSE_TYPE1
};

char* athena_kpathsea_find (const char* name,
                            athena_kpathsea_format format);

#endif // defined KPATHSEA_LOOKUP_H
