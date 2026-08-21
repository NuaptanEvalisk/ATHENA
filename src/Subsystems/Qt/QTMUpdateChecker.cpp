/******************************************************************************
* MODULE     : QTMUpdateChecker.cpp
* DESCRIPTION: GitHub release update checks for ATHENA
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMUpdateChecker.hpp"

#include "QTMToast.hpp"
#include "scheme.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QtGlobal>

namespace {

struct ReleaseInfo {
  QString tag;
  QString name;
  QString url;
};

struct VersionKey {
  QVector<int> numbers;
  bool prerelease= false;
  QString suffix;
  bool valid= false;
};

static bool scheduled= false;
static bool checked= false;
static QPointer<QNetworkAccessManager> manager;

static string
to_tm_string_update (const QString& text) {
  QByteArray bytes= text.toUtf8 ();
  return string (bytes.constData ());
}

static QString
current_version () {
#ifdef ATHENA_APP_VERSION
  return QString::fromLatin1 (ATHENA_APP_VERSION);
#elif defined(ATHENA_VERSION)
  return QString::fromLatin1 (ATHENA_VERSION);
#else
  return QString ();
#endif
}

static QString
normalize_version_text (QString text) {
  text= text.trimmed ();
  if (text.startsWith ('v', Qt::CaseInsensitive)) text.remove (0, 1);

  QRegularExpression firstDigit ("\\d");
  QRegularExpressionMatch match= firstDigit.match (text);
  if (match.hasMatch () && match.capturedStart () > 0)
    text= text.mid (match.capturedStart ());
  return text;
}

static VersionKey
parse_version (QString text) {
  VersionKey key;
  text= normalize_version_text (text);
  if (text.isEmpty ()) return key;

  int suffixPos= text.indexOf ('-');
  QString core= suffixPos >= 0 ? text.left (suffixPos) : text;
  key.prerelease= suffixPos >= 0;
  key.suffix= suffixPos >= 0 ? text.mid (suffixPos + 1).toLower () : QString ();

  QRegularExpression numberRe ("\\d+");
  QRegularExpressionMatchIterator it= numberRe.globalMatch (core);
  while (it.hasNext ()) key.numbers << it.next ().captured ().toInt ();
  key.valid= !key.numbers.isEmpty ();
  return key;
}

static int
compare_versions (QString a, QString b) {
  VersionKey va= parse_version (a);
  VersionKey vb= parse_version (b);
  if (!va.valid && !vb.valid) return QString::compare (a, b, Qt::CaseInsensitive);
  if (!va.valid) return -1;
  if (!vb.valid) return 1;

  int n= qMax (va.numbers.size (), vb.numbers.size ());
  for (int i=0; i<n; i++) {
    int ai= i < va.numbers.size () ? va.numbers[i] : 0;
    int bi= i < vb.numbers.size () ? vb.numbers[i] : 0;
    if (ai < bi) return -1;
    if (ai > bi) return 1;
  }

  if (va.prerelease != vb.prerelease)
    return va.prerelease ? -1 : 1;
  return QString::compare (va.suffix, vb.suffix, Qt::CaseInsensitive);
}

static bool
preference_enabled () {
  return get_preference ("check for updates", "on") == "on";
}

static bool
newer_than_current (const ReleaseInfo& release) {
  QString current= current_version ();
  if (current.trimmed ().isEmpty ()) return false;
  return compare_versions (release.tag, current) > 0;
}

static void
notify_update (const ReleaseInfo& release) {
  QString title= QString ("ATHENA update available");
  QString shown= release.name.trimmed ().isEmpty () ? release.tag : release.name;
  QString body= QString ("Release %1 is newer than this ATHENA build (%2).\n%3")
                  .arg (shown, current_version (), release.url);
  qtm_show_toast (to_tm_string_update (body), to_tm_string_update (title));
}

static void
handle_release_reply (QNetworkReply* reply) {
  if (reply == nullptr) return;

  if (reply->error () != QNetworkReply::NoError) return;
  QByteArray bytes= reply->readAll ();

  QJsonParseError error;
  QJsonDocument doc= QJsonDocument::fromJson (bytes, &error);
  if (error.error != QJsonParseError::NoError || !doc.isArray ()) return;

  ReleaseInfo best;
  for (const QJsonValue& value: doc.array ()) {
    QJsonObject object= value.toObject ();
    if (object.value ("draft").toBool ()) continue;

    ReleaseInfo candidate;
    candidate.tag= object.value ("tag_name").toString ();
    candidate.name= object.value ("name").toString ();
    candidate.url= object.value ("html_url").toString ();
    if (candidate.tag.trimmed ().isEmpty ()) continue;
    if (candidate.url.trimmed ().isEmpty ())
      candidate.url= "https://github.com/NuaptanEvalisk/ATHENA/releases";

    if (best.tag.isEmpty () || compare_versions (candidate.tag, best.tag) > 0)
      best= candidate;
  }

  if (!best.tag.isEmpty () && newer_than_current (best))
    notify_update (best);
}

static void
check_updates_now () {
  if (checked || !preference_enabled ()) return;
  checked= true;

  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;
  if (!manager) manager= new QNetworkAccessManager (app);

  QNetworkRequest request (
    QUrl ("https://api.github.com/repos/NuaptanEvalisk/ATHENA/releases"));
  request.setRawHeader ("Accept", "application/vnd.github+json");
  request.setRawHeader ("User-Agent", "ATHENA Update Checker");
  request.setTransferTimeout (10000);

  QNetworkReply* reply= manager->get (request);
  QObject::connect (reply, &QNetworkReply::finished, reply, [reply] () {
    handle_release_reply (reply);
    reply->deleteLater ();
  });
}

} // namespace

void
qtm_schedule_update_check () {
  if (scheduled || !preference_enabled ()) return;
  scheduled= true;

  QCoreApplication* app= QCoreApplication::instance ();
  if (app == nullptr) return;
  QTimer::singleShot (8000, app, [] () { check_updates_now (); });
}
