/******************************************************************************
* MODULE     : QTMSystemPowerMonitor.cpp
* DESCRIPTION: Desktop power-state notifications for automatic background work
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMSystemPowerMonitor.hpp"

#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#endif

namespace {

#if defined(Q_OS_LINUX)
QVariant
dbus_property (const QDBusConnection& bus, const QString& service,
               const QString& path, const QString& interface,
               const QString& name) {
  QDBusInterface properties (
    service, path, QStringLiteral ("org.freedesktop.DBus.Properties"), bus);
  if (!properties.isValid ()) return {};
  QDBusReply<QDBusVariant> reply= properties.call (
    QStringLiteral ("Get"), interface, name);
  return reply.isValid () ? reply.value ().variant (): QVariant {};
}
#endif

} // namespace

QTMSystemPowerMonitor::QTMSystemPowerMonitor (QObject* parent): QObject (parent) {
#if defined(Q_OS_LINUX)
  QDBusConnection bus= QDBusConnection::systemBus ();
  if (bus.isConnected ()) {
    (void) bus.connect (
      QStringLiteral ("org.freedesktop.login1"), QStringLiteral ("/org/freedesktop/login1"),
      QStringLiteral ("org.freedesktop.login1.Manager"), QStringLiteral ("PrepareForSleep"),
      this, SLOT (prepareForSleep(bool)));
    const QString upower= QStringLiteral ("org.freedesktop.UPower");
    const QString profiles= QStringLiteral ("org.freedesktop.UPower.PowerProfiles");
    const QString legacyProfiles= QStringLiteral ("net.hadess.PowerProfiles");
    for (const QString& service: {upower, profiles, legacyProfiles}) {
      auto* watcher= new QDBusServiceWatcher (
        service, bus,
        QDBusServiceWatcher::WatchForRegistration |
          QDBusServiceWatcher::WatchForUnregistration,
        this);
      QObject::connect (
        watcher, &QDBusServiceWatcher::serviceRegistered,
        this, [this] (const QString&) { refresh (); });
      QObject::connect (
        watcher, &QDBusServiceWatcher::serviceUnregistered,
        this, [this] (const QString&) { refresh (); });
    }

    (void) bus.connect (
      upower, QStringLiteral ("/org/freedesktop/UPower"),
      QStringLiteral ("org.freedesktop.DBus.Properties"),
      QStringLiteral ("PropertiesChanged"), this,
      SLOT (propertiesChanged(QString,QVariantMap,QStringList)));
    (void) bus.connect (
      profiles, QStringLiteral ("/org/freedesktop/UPower/PowerProfiles"),
      QStringLiteral ("org.freedesktop.DBus.Properties"),
      QStringLiteral ("PropertiesChanged"), this,
      SLOT (propertiesChanged(QString,QVariantMap,QStringList)));
    (void) bus.connect (
      legacyProfiles, QStringLiteral ("/net/hadess/PowerProfiles"),
      QStringLiteral ("org.freedesktop.DBus.Properties"),
      QStringLiteral ("PropertiesChanged"), this,
      SLOT (propertiesChanged(QString,QVariantMap,QStringList)));
  }
#endif
  refresh ();
}

void
QTMSystemPowerMonitor::prepareForSleep (bool sleeping) {
  emit sleepChanged (sleeping);
}

void
QTMSystemPowerMonitor::propertiesChanged (
  const QString&, const QVariantMap&, const QStringList&) {
  refresh ();
}

void
QTMSystemPowerMonitor::refresh () {
  State next;
#if defined(Q_OS_LINUX)
  const QDBusConnection bus= QDBusConnection::systemBus ();
  if (bus.isConnected ()) {
    const QVariant onBattery= dbus_property (
      bus, QStringLiteral ("org.freedesktop.UPower"),
      QStringLiteral ("/org/freedesktop/UPower"),
      QStringLiteral ("org.freedesktop.UPower"),
      QStringLiteral ("OnBattery"));
    if (onBattery.isValid () && onBattery.canConvert<bool> ())
      next.supply= onBattery.toBool () ? Supply::Battery : Supply::External;

    QVariant profile= dbus_property (
      bus, QStringLiteral ("org.freedesktop.UPower.PowerProfiles"),
      QStringLiteral ("/org/freedesktop/UPower/PowerProfiles"),
      QStringLiteral ("org.freedesktop.UPower.PowerProfiles"),
      QStringLiteral ("ActiveProfile"));
    if (!profile.isValid ())
      profile= dbus_property (
        bus, QStringLiteral ("net.hadess.PowerProfiles"),
        QStringLiteral ("/net/hadess/PowerProfiles"),
        QStringLiteral ("net.hadess.PowerProfiles"),
        QStringLiteral ("ActiveProfile"));
    if (profile.isValid ()) {
      next.powerSaverKnown= true;
      next.powerSaver= profile.toString () == QStringLiteral ("power-saver");
    }
  }
#endif
  setState (next);
}

void
QTMSystemPowerMonitor::setState (State state) {
  if (state.supply == state_.supply &&
      state.powerSaver == state_.powerSaver &&
      state.powerSaverKnown == state_.powerSaverKnown)
    return;
  state_= state;
  emit stateChanged ();
}
