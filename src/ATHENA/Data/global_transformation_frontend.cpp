// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#include "global_transformation_frontend.hpp"
#include "guile_tm.hpp"
#include "native_interfaces.hpp"
#include "vault.hpp"
#include "file.hpp"
#include "server.hpp"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
struct registration {
  string name;
  object transformer= object (false);
  std::string filename, error;
  bool registered= false;
};
thread_local registration* loading= nullptr;
struct registration_scope {
  registration* previous;
  registration_scope (registration& value): previous (loading) { loading= &value; }
  ~registration_scope () { loading= previous; }
};
SCM load_body (void* data) {
  return scm_c_primitive_load (static_cast<registration*> (data)->filename.c_str ());
}
SCM load_in_module (void* data) {
  // Guile's dynamic extent restores the caller's module even when a script throws.
  return scm_c_call_with_current_module (scm_current_module (), load_body, data);
}
SCM load_error (void* data, SCM key, SCM arguments) {
  SCM message= scm_simple_format (SCM_BOOL_F, scm_from_utf8_string ("~S: ~S"),
                                 scm_list_2 (key, arguments));
  size_t size= 0;
  char* bytes= scm_to_utf8_stringn (message, &size);
  static_cast<registration*> (data)->error.assign (bytes, size);
  std::free (bytes);
  return SCM_BOOL_F;
}
QString qstring (string value) { return QString::fromUtf8 (value.c_str (), N(value)); }
}

void register_global_transformation (string name, object transformer) {
  if (!loading)
    throw std::runtime_error ("Register a transformer while loading a transformation script");
  if (loading->registered)
    throw std::runtime_error ("A transformation script must register exactly one transformer");
  if (N(name) == 0 || !is_procedure (transformer))
    throw std::runtime_error ("register-global-transformation expects a non-empty name and a procedure");
  loading->name= name;
  loading->transformer= transformer;
  loading->registered= true;
}

object load_global_transformation_script (url script) {
  string filename= concretize (script);
  if (QFileInfo (qstring (filename)).suffix ().toLower () != "scm")
    throw std::runtime_error ("Select a Scheme (.scm) transformation script");
  registration value;
  value.filename.assign (filename.c_str (), N(filename));
  {
    registration_scope scope (value);
    scm_c_catch (SCM_BOOL_T, load_in_module, &value, load_error, &value, nullptr, nullptr);
  }
  if (!value.error.empty ())
    throw std::runtime_error ("Could not load the selected Scheme transformation script\n\n" + value.error);
  if (!value.registered)
    throw std::runtime_error ("The script did not call register-global-transformation");
  return list_object (object (value.name), value.transformer);
}

void run_global_transformation () {
  if (!vault_active ()) {
    QMessageBox::information (qApp->activeWindow (), "Global transformation",
                             "Load a Vault before running a global transformation");
    return;
  }
  QString root= qstring (concretize (vault_get_root ()));
  QString dependencies= QDir (root).filePath ("dependencies");
  QString selected= QFileDialog::getOpenFileName (qApp->activeWindow (),
    "Run global transformation", QDir (dependencies).exists ()? dependencies: root,
    "Scheme transformations (*.scm)");
  if (selected.isEmpty ()) return;
  try {
    QByteArray bytes= selected.toUtf8 ();
    object script= load_global_transformation_script (url_system (string (bytes.constData (), bytes.size ())));
    tree result= athena_global_transformation_run (cadr (script), as_string (car (script)));
    get_server ()->set_message (result[3], result[0] == "error"?
      tree ("Global transformation error"): tree ("Global transformation"));
  }
  catch (const std::exception& error) {
    QMessageBox::warning (qApp->activeWindow (), "Global transformation",
                         QString::fromUtf8 (error.what ()));
  }
}
