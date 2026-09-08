/******************************************************************************
* MODULE     : ispell.cpp
* DESCRIPTION: In-process Hunspell, with thread-owned dictionaries
* COPYRIGHT  : (C) 2026  Felix Lian
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "Ispell/ispell.hpp"
#include "convert.hpp"
#include "locale.hpp"
#include "file.hpp"
#include <hunspell.hxx>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextCodec>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace {

std::string bytes (string s) {
  c_string c (s);
  return std::string (c, N(s));
}

struct personal_dictionary {
  std::set<std::string> words; // UTF-8, independent of system dictionary encoding
  QString file;
  bool loaded= false;
  bool dirty= false;
};

std::mutex personal_mutex;
std::map<std::string, personal_dictionary> personal_dictionaries;
std::atomic<unsigned long> dictionary_revision {1};

personal_dictionary& personal (const std::string& locale) {
  auto& p= personal_dictionaries[locale];
  if (p.loaded) return p;
  p.loaded= true;
  p.file= QString::fromStdString (bytes (concretize (
    url_system ("$ATHENA_HOME_PATH/system/spelling")))) + "/" +
    QString::fromStdString (locale) + ".txt";
  QFile file (p.file);
  if (file.open (QIODevice::ReadOnly))
    while (!file.atEnd ()) {
      QByteArray word= file.readLine ().trimmed ();
      if (!word.isEmpty ()) p.words.insert (word.toStdString ());
    }
  // The old -a -i utf-8 subprocess stored additions here. Read it without
  // modifying it; new additions are saved atomically in ATHENA's own profile.
  QFile legacy (QDir::homePath () + "/.hunspell_" +
                QString::fromStdString (locale));
  if (legacy.open (QIODevice::ReadOnly)) {
    bool first= true;
    while (!legacy.atEnd ()) {
      QByteArray word= legacy.readLine ().trimmed ();
      bool count= false;
      word.toUInt (&count);
      if (!(first && count) && !word.isEmpty ())
        p.words.insert (word.toStdString ());
      first= false;
    }
  }
  return p;
}

QString dictionary_base (const std::string& locale) {
  QStringList dirs= QString::fromLocal8Bit (qgetenv ("DICPATH"))
    .split (QDir::listSeparator (), Qt::SkipEmptyParts);
  dirs << QString::fromStdString (bytes (concretize (
            url_system ("$ATHENA_PATH/dictionaries"))))
       << QDir::homePath () + "/.local/share/hunspell"
       << "/usr/local/share/hunspell" << "/usr/share/hunspell"
       << "/usr/local/share/myspell" << "/usr/share/myspell"
       << "/usr/share/myspell/dicts"
       << QDir::homePath () + "/Library/Spelling" << "/Library/Spelling";
  QString name= QString::fromStdString (locale);
  // Prefer the exact locale everywhere before trying a language-only dictionary.
  for (const QString& candidate: {name, name.section ('_', 0, 0)})
    for (const QString& dir: dirs) {
      QString base= QDir (dir).filePath (candidate);
      if (QFile::exists (base + ".aff") && QFile::exists (base + ".dic"))
        return base;
    }
  return {};
}

struct dictionary {
  std::string locale;
  std::unique_ptr<Hunspell> engine;
  QTextCodec* codec= nullptr;
  std::set<std::string> accepted;
  unsigned long revision= 0;
  string error;

  explicit dictionary (string lan) {
    locale= bytes (language_to_locale (lan));
    // These historical locale names differ from the installed dictionary names.
    if (lan == "greek") locale= "el_GR";
    if (lan == "slovene") locale= "sl_SI";
    if (lan == "swedish") locale= "sv_SE";
    QString base= dictionary_base (locale);
    if (base.isEmpty ()) {
      error= "Error: Hunspell dictionary not found for " * lan;
      return;
    }
    engine= std::make_unique<Hunspell> (
      QFile::encodeName (base + ".aff").constData (),
      QFile::encodeName (base + ".dic").constData ());
    codec= QTextCodec::codecForName (engine->get_dic_encoding ());
    if (!codec) {
      error= "Error: unsupported Hunspell dictionary encoding";
      engine.reset ();
    }
  }

  std::string encode (const std::string& utf8) const {
    return codec->fromUnicode (QString::fromUtf8 (
      utf8.data (), (int) utf8.size ())).toStdString ();
  }

  void sync () {
    auto current= dictionary_revision.load (std::memory_order_acquire);
    if (!engine || revision == current) return;
    std::lock_guard<std::mutex> lock (personal_mutex);
    for (const auto& word: personal (locale).words) engine->add (encode (word));
    revision= current;
  }

  bool test (const std::string& utf8) {
    sync ();
    // Missing dictionaries must not mark every word as an error.
    if (!engine || accepted.count (utf8)) return true;
    return engine->spell (encode (utf8));
  }
};

dictionary& get_dictionary (string lan) {
  thread_local std::map<std::string, std::unique_ptr<dictionary>> dictionaries;
  auto& d= dictionaries[bytes (lan)];
  if (!d) d= std::make_unique<dictionary> (lan);
  return *d;
}
} // namespace

unsigned long
ispell_dictionary_revision () {
  return dictionary_revision.load (std::memory_order_acquire);
}

string
ispell_start (string lan) {
  auto& d= get_dictionary (lan);
  return d.engine? string ("ok"): d.error;
}

bool
ispell_test (string lan, string word) {
  return lan == "verbatim" || get_dictionary (lan).test (bytes (cork_to_utf8 (word)));
}

tree
ispell_check (string lan, string word) {
  if (lan == "verbatim") return "ok";
  auto& d= get_dictionary (lan);
  if (!d.engine) return d.error;
  std::string utf8= bytes (cork_to_utf8 (word));
  if (d.test (utf8)) return "ok";
  tree result (TUPLE, word);
  for (const auto& suggestion: d.engine->suggest (d.encode (utf8))) {
    QByteArray decoded= d.codec->toUnicode (suggestion.data (),
                                           (int) suggestion.size ()).toUtf8 ();
    result << utf8_to_cork (string (decoded.constData (), decoded.size ()));
  }
  return result;
}

void
ispell_accept (string lan, string word) {
  get_dictionary (lan).accepted.insert (bytes (cork_to_utf8 (word)));
}

void
ispell_insert (string lan, string word) {
  auto& d= get_dictionary (lan);
  std::string utf8= bytes (cork_to_utf8 (word));
  if (utf8.empty () || utf8.find_first_of ("\r\n") != std::string::npos) return;
  std::lock_guard<std::mutex> lock (personal_mutex);
  auto& p= personal (d.locale);
  if (p.words.insert (utf8).second) {
    p.dirty= true;
    dictionary_revision.fetch_add (1, std::memory_order_release);
  }
}

void
ispell_done (string lan) {
  auto& d= get_dictionary (lan);
  d.accepted.clear ();
  std::lock_guard<std::mutex> lock (personal_mutex);
  auto& p= personal (d.locale);
  if (!p.dirty) return;
  QDir ().mkpath (QFileInfo (p.file).absolutePath ());
  QSaveFile file (p.file);
  bool ok= file.open (QIODevice::WriteOnly);
  if (ok) {
    for (const auto& word: p.words) {
      std::string line= word + "\n";
      if (file.write (line.data (), line.size ()) != (qint64) line.size ()) {
        ok= false;
        break;
      }
    }
    if (ok) ok= file.commit ();
    else file.cancelWriting ();
  }
  if (ok) p.dirty= false;
  else std_warning << "Could not save Hunspell personal dictionary "
                   << string (p.file.toUtf8 ().constData ()) << LF;
}
