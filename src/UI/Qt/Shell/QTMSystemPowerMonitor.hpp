/******************************************************************************
* MODULE     : QTMSystemPowerMonitor.hpp
* DESCRIPTION: Desktop power-state notifications for automatic background work
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMSYSTEMPOWERMONITOR_HPP
#define QTMSYSTEMPOWERMONITOR_HPP

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QTMSystemPowerMonitor: public QObject {
  Q_OBJECT

public:
  enum class Supply {
    Unknown,
    External,
    Battery
  };

  struct State {
    Supply supply= Supply::Unknown;
    bool powerSaver= false;
    bool powerSaverKnown= false;
  };

  explicit QTMSystemPowerMonitor (QObject* parent= nullptr);
  State state () const { return state_; }

signals:
  void stateChanged ();
  void sleepChanged (bool sleeping);

private slots:
  void prepareForSleep (bool sleeping);
  void propertiesChanged (const QString& interface,
                          const QVariantMap& changed,
                          const QStringList& invalidated);

private:
  void refresh ();
  void setState (State state);

  State state_;
};

#endif // QTMSYSTEMPOWERMONITOR_HPP
