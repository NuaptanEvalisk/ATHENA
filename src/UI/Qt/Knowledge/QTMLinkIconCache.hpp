/******************************************************************************
* MODULE     : QTMLinkIconCache.hpp
* DESCRIPTION: Nonblocking actor requests for GUI-owned card-link icons
* COPYRIGHT  : (C) 2026  Nuaptan F. Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTM_LINK_ICON_CACHE_HPP
#define QTM_LINK_ICON_CACHE_HPP

#include <QApplication>
#include <QFileInfo>
#include <QPixmap>
#include <QSaveFile>
#include <QThread>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <utility>

class QTMLinkIconCache {
public:
  using observer= std::pair<std::uint64_t, std::uint64_t>;
  using renderer= std::function<QPixmap ()>;
  using notifier= std::function<void (observer)>;

  QString request (QApplication* app, const QString& path, observer owner,
                   renderer render, notifier notify) {
    if (!app) return {};
    {
      std::lock_guard<std::mutex> lock (mutex_);
      auto found= entries_.find (path);
      if (found != entries_.end ()) {
        if (found->second.pending && owner.first != 0)
          found->second.observers.insert (owner);
        return found->second.ready ? path : QString ();
      }
      if (QFileInfo::exists (path)) return path;
      auto& entry= entries_[path];
      if (owner.first != 0) entry.observers.insert (owner);
    }
    auto finish= [this, path, render, notify] {
      QPixmap pixmap= render ();
      QSaveFile output (path);
      bool ready= !pixmap.isNull () && output.open (QIODevice::WriteOnly) &&
                  pixmap.save (&output, "PNG") && output.commit ();
      std::set<observer> observers;
      {
        std::lock_guard<std::mutex> lock (mutex_);
        auto& entry= entries_.at (path);
        entry.pending= false;
        entry.ready= ready;
        observers.swap (entry.observers);
      }
      // Never hold the cache lock while notifying an actor. Failed icons keep
      // the semantic fallback and must not cause an invalidation/retry loop.
      if (ready)
        for (auto owner: observers) notify (owner);
    };
    if (QThread::currentThread () == app->thread ()) {
      finish ();
      std::lock_guard<std::mutex> lock (mutex_);
      return entries_.at (path).ready ? path : QString ();
    }
    // The GUI may be synchronously waiting for this very caller's actor.
    QMetaObject::invokeMethod (app, std::move (finish), Qt::QueuedConnection);
    return {};
  }

private:
  struct entry {
    bool pending= true;
    bool ready= false;
    std::set<observer> observers;
  };
  std::mutex mutex_;
  std::map<QString, entry> entries_;
};

#endif
