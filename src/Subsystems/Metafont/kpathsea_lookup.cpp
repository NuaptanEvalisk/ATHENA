/******************************************************************************
* MODULE     : kpathsea_lookup.cpp
* DESCRIPTION: Isolated interface to libkpathsea
*******************************************************************************/

#include "kpathsea_lookup.hpp"

#ifdef USE_KPATHSEA_API
#include <QCoreApplication>
#include <kpathsea/kpathsea.h>

namespace {
struct kpathsea_context {
  kpathsea instance= kpathsea_new ();
  kpathsea_context () {
    QByteArray executable= QCoreApplication::applicationFilePath ().toLocal8Bit ();
    kpathsea_set_program_name (instance, executable.constData (), "ATHENA");
  }
  ~kpathsea_context () { kpathsea_finish (instance); }
};
}

char*
athena_kpathsea_find (const char* name, athena_kpathsea_format format) {
  static thread_local kpathsea_context context;
  kpse_file_format_type kpse_format= kpse_tfm_format;
  if (format == ATHENA_KPSE_PK) kpse_format= kpse_pk_format;
  if (format == ATHENA_KPSE_TYPE1) kpse_format= kpse_type1_format;
  return kpathsea_find_file (context.instance, name, kpse_format, true);
}
#endif
