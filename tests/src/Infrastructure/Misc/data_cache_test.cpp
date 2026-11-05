/******************************************************************************
* MODULE     : data_cache_test.cpp
* DESCRIPTION: Declarative resources must not use stale directory-based caches
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "data_cache.hpp"
#include "file.hpp"
#include "sys_utils.hpp"

bool headless_mode= true;
bool is_headless () { return true; }

class DataCacheTest: public QObject {
  Q_OBJECT
  QTemporaryDir temporary;

private slots:
  void initTestCase () {
    QVERIFY (temporary.isValid ());
    QVERIFY (QDir ().mkpath (temporary.path () + "/install/misc/input"));
    QVERIFY (QDir ().mkpath (temporary.path () + "/home/system/cache"));
    qputenv ("ATHENA_PATH", (temporary.path () + "/install").toUtf8 ());
    qputenv ("ATHENA_HOME_PATH", (temporary.path () + "/home").toUtf8 ());
  }

  void jsonIgnoresPersistedContentCacheAfterInPlaceEdit () {
    const QString filename= temporary.path () + "/install/misc/input/actions.json";
    QFile file (filename);
    const QByteArray old_data= "{\"action\":\"old\"}";
    const QByteArray new_data= "{\"action\":\"new\"}";
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (old_data), old_data.size ());
    file.close ();

    const string name (filename.toUtf8 ().constData ());
    const url resource= url_system (name);
    const url parent= url_parent (resource);
    const int parent_time= last_modified (parent, false);
    cache_set ("file_cache", name, string (old_data.constData ()));
    cache_set ("validate_cache.scm", concretize (parent), as_string (parent_time));
    cache_save ("file_cache");
    cache_save ("validate_cache.scm");
    cache_refresh ();
    QVERIFY (is_cached ("file_cache", name));
    QVERIFY (is_up_to_date (parent));

    QVERIFY (file.open (QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE (file.write (new_data), new_data.size ());
    file.close ();
    QCOMPARE (last_modified (parent, false), parent_time);
    string loaded;
    QVERIFY (!load_string (resource, loaded, false));
    QCOMPARE (loaded, string (new_data.constData ()));
  }
};

QTEST_GUILESS_MAIN (DataCacheTest)
#include "data_cache_test.moc"
