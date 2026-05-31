
/******************************************************************************
* MODULE     : preferences.cpp
* DESCRIPTION: User preferences for TeXmacs
* COPYRIGHT  : (C) 2012  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "boot.hpp"
#include "file.hpp"
#include "sys_utils.hpp"
#include "analyze.hpp"
#include "convert.hpp"
#include "merge_sort.hpp"
#include "iterator.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

/******************************************************************************
* Changing the user preferences
******************************************************************************/

bool user_prefs_modified= false;
hashmap<string,string> user_prefs ("");
hashmap<string,string> user_prefs_default ("");
hashmap<string,bool> user_prefs_string_default (true);
url user_prefs_file= "$ATHENA_HOME_PATH/system/preferences.json";
void notify_preference (string var);

static QString
to_qstring (string s) {
  return QString::fromUtf8 (as_charp (s), N(s));
}

static string
from_qstring (const QString& s) {
  QByteArray bytes= s.toUtf8 ();
  return string (bytes.constData ());
}

static bool
has_suffix (string s, string suf) {
  return N(s) >= N(suf) && s (N(s) - N(suf), N(s)) == suf;
}

static url
with_json_suffix (url u) {
  string s= as_string (u);
  if (has_suffix (s, ".json")) return u;
  if (has_suffix (s, ".scm")) return url (s (0, N(s) - 4) * ".json");
  return url (s * ".json");
}

bool
has_user_preference (string var) {
  return user_prefs->contains (var);
}

void
register_user_preference (string var, string def, bool string_def) {
  if (!user_prefs_default->contains (var)) {
    user_prefs_default (var)= def;
    user_prefs_string_default (var)= string_def;
  }
}

bool
user_preference_default_is_string (string var) {
  if (user_prefs_string_default->contains (var))
    return user_prefs_string_default[var];
  return true;
}

void
set_user_preference (string var, string val) {
  if (val == "default") user_prefs->reset (var);
  else user_prefs (var)= val;
  user_prefs_modified= true;
  notify_preference (var);
}

void
reset_user_preference (string var) {
  user_prefs->reset (var);
  user_prefs_modified= true;
  notify_preference (var);
}

string
get_user_preference (string var, string val) {
  if (user_prefs->contains (var)) return user_prefs[var];
  if (user_prefs_default->contains (var)) return user_prefs_default[var];
  else return val;
}

/******************************************************************************
* Loading and saving user preferences
******************************************************************************/

static hashmap<string,string>
read_scheme_user_preferences (url prefs_file) {
  hashmap<string,string> prefs ("");
  string s;
  tree p (TUPLE);
  if (!load_string (prefs_file, s, false))
    p= block_to_scheme_tree (s);
  while (is_func (p, TUPLE, 1)) p= p[0];
  for (int i=0; i<N(p); i++)
    if (is_func (p[i], TUPLE, 2) &&
        is_atomic (p[i][0]) && is_atomic (p[i][1]) &&
        is_quoted (p[i][0]->label) && is_quoted (p[i][1]->label)) {
      string var= scm_unquote (p[i][0]->label);
      string val= scm_unquote (p[i][1]->label);
      prefs (var)= val;
    }
  return prefs;
}

static hashmap<string,string>
read_json_user_preferences (url prefs_file, bool& ok) {
  ok= false;
  hashmap<string,string> prefs ("");
  string s;
  if (load_string (prefs_file, s, false)) return prefs;

  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (QByteArray (as_charp (s), N(s)),
                                              &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject ()) {
    std_error << "Invalid preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject root= doc.object ();
  if (root.value ("format").toString () != "athena-preferences" ||
      root.value ("version").toInt () != 1 ||
      !root.value ("preferences").isObject ()) {
    std_error << "Unsupported preferences JSON in " << prefs_file << LF;
    return prefs;
  }

  QJsonObject obj= root.value ("preferences").toObject ();
  for (QJsonObject::const_iterator it= obj.constBegin ();
       it != obj.constEnd (); ++it) {
    if (!it.value ().isString ()) continue;
    prefs (from_qstring (it.key ()))= from_qstring (it.value ().toString ());
  }

  ok= true;
  return prefs;
}

static void
write_scheme_user_preferences (url prefs_file) {
  iterator<string> it= iterate (user_prefs);
  array<string> a;
  while (it->busy ())
    a << it->next ();
  merge_sort (a);
  string s;
  for (int i=0; i<N(a); i++)
    s << "(" << scm_quote (a[i])
      << " " << scm_quote (user_prefs[a[i]]) << ")\n";
  if (save_string (prefs_file, s))
    std_warning << "The user preferences could not be saved\n";
}

static void
write_json_user_preferences (url prefs_file) {
  iterator<string> it= iterate (user_prefs);
  QJsonObject prefs;
  while (it->busy ()) {
    string key= it->next ();
    prefs.insert (to_qstring (key), to_qstring (user_prefs[key]));
  }

  QJsonObject root;
  root.insert ("format", "athena-preferences");
  root.insert ("version", 1);
  root.insert ("preferences", prefs);

  QJsonDocument doc (root);
  QByteArray bytes= doc.toJson (QJsonDocument::Indented);
  if (save_string (prefs_file, string (bytes.constData ())))
    std_warning << "The user preferences could not be saved\n";
}

static hashmap<string,string>
read_user_preferences (url prefs_file, url& canonical_file) {
  bool json_ok= false;
  if (has_suffix (as_string (prefs_file), ".json")) {
    bool json_exists= exists (prefs_file);
    hashmap<string,string> prefs= read_json_user_preferences (prefs_file,
                                                              json_ok);
    canonical_file= prefs_file;
    if (json_ok) return prefs;

    url legacy_file= url (as_string (prefs_file) (0,
                         N(as_string (prefs_file)) - 5) * ".scm");
    if (exists (legacy_file)) {
      if (json_exists)
        std_warning << "preferences: falling back to legacy preferences file "
                    << legacy_file << LF;
      else
        cout << "preferences: importing legacy preferences file "
             << legacy_file << LF;
      prefs= read_scheme_user_preferences (legacy_file);
      if (!json_exists) {
        user_prefs= prefs;
        write_json_user_preferences (prefs_file);
      }
      return prefs;
    }
    return prefs;
  }

  canonical_file= with_json_suffix (prefs_file);
  if (exists (canonical_file)) {
    hashmap<string,string> prefs= read_json_user_preferences (canonical_file,
                                                             json_ok);
    if (json_ok) return prefs;
    if (exists (prefs_file))
      return read_scheme_user_preferences (prefs_file);
    return prefs;
  }

  hashmap<string,string> prefs= read_scheme_user_preferences (prefs_file);
  user_prefs= prefs;
  write_json_user_preferences (canonical_file);
  return prefs;
}

static void
write_user_preferences (url prefs_file) {
  if (has_suffix (as_string (prefs_file), ".scm"))
    write_scheme_user_preferences (prefs_file);
  else
    write_json_user_preferences (prefs_file);
}

void
load_user_preferences () {
  load_user_preferences ("$ATHENA_HOME_PATH/system/preferences.json");
}

void
load_user_preferences (url prefs_file) {
  save_user_preferences ();
  user_prefs= read_user_preferences (prefs_file, user_prefs_file);
  user_prefs_modified= false;
}

void
dump_user_preferences (url prefs_file) {
  write_user_preferences (prefs_file);
}

void
save_user_preferences () {
  if (!user_prefs_modified) return;
  write_user_preferences (user_prefs_file);
  user_prefs_modified= false;
}
