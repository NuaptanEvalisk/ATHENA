
import os
import sys

def patch_file(file_path, old_patterns, new):
    if not os.path.exists(file_path):
        print(f"File not found: {file_path}")
        return
    
    with open(file_path, 'r') as f:
        content = f.read()
    
    original_content = content
    for old in old_patterns:
        content = content.replace(old, new)
    
    if content != original_content:
        with open(file_path, 'w') as f:
            f.write(content)
        print(f"Patched {file_path}")
    else:
        print(f"No changes needed for {file_path}")

def patch_ads_floating_windows(base_dir):
    # ATHENA keeps ADS' custom floating title bar on KDE because native KWin
    # title bars do not provide the live move events ADS needs for redocking.
    # Add side-edge snapping to the ADS drag-release path instead.
    path = os.path.join(base_dir, 'src/FloatingDockContainer.cpp')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    if '#include <QScreen>' not in content:
        content = content.replace('#include <QTime>\n',
                                  '#include <QTime>\n#include <QScreen>\n')
    if 'athenaSnapFloatingContainerToScreenEdge' not in content:
        content = content.replace(
            'namespace ads\n{\n',
            '''namespace ads
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static void
athenaSnapFloatingContainerToScreenEdge(QWidget* widget)
{
    if (widget == nullptr || !widget->isVisible())
    {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr)
    {
        screen = widget->screen();
    }
    if (screen == nullptr)
    {
        return;
    }

    QRect area = screen->availableGeometry();
    QPoint cursor = QCursor::pos();
    int threshold = qMax(24, area.width() / 80);
    QRect target;
    if (qAbs(cursor.x() - area.left()) <= threshold)
    {
        target = QRect(area.left(), area.top(),
                       area.width() / 2, area.height());
    }
    else if (qAbs(cursor.x() - area.right()) <= threshold)
    {
        target = QRect(area.left() + area.width() / 2, area.top(),
                       area.width() - area.width() / 2, area.height());
    }
    else
    {
        return;
    }

    widget->setGeometry(target);
}
#endif

''',
            1)
    content = content.replace(
        '''\td->titleMouseReleaseEvent();
}''',
        '''\td->titleMouseReleaseEvent();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tathenaSnapFloatingContainerToScreenEdge(this);
#endif
}''')

    if content != original_content:
        with open(path, 'w') as f:
            f.write(content)
        print(f"Patched {path}")
    else:
        print(f"No changes needed for {path}")

def patch_ads_qt6_private_gui(base_dir):
    path = os.path.join(base_dir, 'src/CMakeLists.txt')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    content = content.replace(
        'find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets REQUIRED)\n',
        '''if(QT_VERSION_MAJOR STREQUAL "6")
  find_package(Qt6 COMPONENTS Core Gui Widgets GuiPrivate REQUIRED)
else()
  find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets REQUIRED)
endif()
''')
    content = content.replace(
        '''target_link_libraries(${library_name} PUBLIC Qt${QT_VERSION_MAJOR}::Core 
                                               Qt${QT_VERSION_MAJOR}::Gui 
                                               Qt${QT_VERSION_MAJOR}::Widgets)
''',
        '''target_link_libraries(${library_name} PUBLIC Qt${QT_VERSION_MAJOR}::Core 
                                               Qt${QT_VERSION_MAJOR}::Gui 
                                               Qt${QT_VERSION_MAJOR}::Widgets)
if(QT_VERSION_MAJOR STREQUAL "6")
    target_link_libraries(${library_name} PRIVATE Qt6::GuiPrivate)
endif()
''')

    if content != original_content:
        with open(path, 'w') as f:
            f.write(content)
        print(f"Patched {path}")
    else:
        print(f"No changes needed for {path}")

def patch_ads_kwin_wayland_docking(base_dir):
    cmake_path = os.path.join(base_dir, 'src/CMakeLists.txt')
    if os.path.exists(cmake_path):
        with open(cmake_path, 'r') as f:
            content = f.read()
        original_content = content
        content = content.replace(
            'find_package(Qt6 COMPONENTS Core Gui Widgets GuiPrivate REQUIRED)\n',
            'find_package(Qt6 COMPONENTS Core Gui Widgets GuiPrivate DBus REQUIRED)\n')
        content = content.replace(
            'find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets REQUIRED)\n',
            'find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets DBus REQUIRED)\n')
        content = content.replace(
            'target_link_libraries(${library_name} PUBLIC Qt${QT_VERSION_MAJOR}::Core \n'
            '                                               Qt${QT_VERSION_MAJOR}::Gui \n'
            '                                               Qt${QT_VERSION_MAJOR}::Widgets)\n',
            'target_link_libraries(${library_name} PUBLIC Qt${QT_VERSION_MAJOR}::Core \n'
            '                                               Qt${QT_VERSION_MAJOR}::Gui \n'
            '                                               Qt${QT_VERSION_MAJOR}::Widgets\n'
            '                                               Qt${QT_VERSION_MAJOR}::DBus)\n')
        if content != original_content:
            with open(cmake_path, 'w') as f:
                f.write(content)
            print(f"Patched {cmake_path}")
        else:
            print(f"No changes needed for {cmake_path}")
    else:
        print(f"File not found: {cmake_path}")

    header_path = os.path.join(base_dir, 'src/FloatingDockContainer.h')
    if os.path.exists(header_path):
        with open(header_path, 'r') as f:
            content = f.read()
        original_content = content
        if '#include <QVariantMap>' not in content:
            content = content.replace('#include <QRubberBand>\n',
                                      '#include <QRubberBand>\n#include <QVariantMap>\n')
        if 'org.athena.KWinDockDragSink' not in content:
            content = content.replace(
                '''class ADS_EXPORT CFloatingDockContainer : public tFloatingWidgetBase, public IFloatingWidget
{
\tQ_OBJECT
''',
                '''class ADS_EXPORT CFloatingDockContainer : public tFloatingWidgetBase, public IFloatingWidget
{
\tQ_OBJECT
\tQ_CLASSINFO("D-Bus Interface", "org.athena.KWinDockDragSink")
''')
        if 'dockDragMoved(const QVariantMap& state)' not in content:
            content = content.replace(
                '''private Q_SLOTS:
\tvoid onDockAreasAddedOrRemoved();
\tvoid onDockAreaCurrentChanged(int Index);
''',
                '''private Q_SLOTS:
\tvoid onDockAreasAddedOrRemoved();
\tvoid onDockAreaCurrentChanged(int Index);

public Q_SLOTS:
\tvoid dockDragMoved(const QVariantMap& state);
\tvoid dockDragDropped(const QVariantMap& state);
\tvoid dockDragCancelled(const QVariantMap& state);
''')
        if 'athenaTryStartKWinDockDrag' not in content:
            content = content.replace(
                '''private:
\tFloatingDockContainerPrivate* d; ///< private data (pimpl)
''',
                '''private:
\tFloatingDockContainerPrivate* d; ///< private data (pimpl)
\tbool athenaTryStartKWinDockDrag(const QPoint& hotSpot);
\tbool athenaKWinDockDragActive() const;
\tvoid athenaFinishKWinDockDrag(const QVariantMap& state, bool dropped);
''')
        if content != original_content:
            with open(header_path, 'w') as f:
                f.write(content)
            print(f"Patched {header_path}")
        else:
            print(f"No changes needed for {header_path}")
    else:
        print(f"File not found: {header_path}")

    cpp_path = os.path.join(base_dir, 'src/FloatingDockContainer.cpp')
    if not os.path.exists(cpp_path):
        print(f"File not found: {cpp_path}")
        return

    with open(cpp_path, 'r') as f:
        content = f.read()
    original_content = content

    if '#include <QDBusConnection>' not in content:
        content = content.replace(
            '#include <QScreen>\n',
            '''#include <QScreen>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <climits>
''')

    if 'athenaVariantMapToRect' not in content:
        content = content.replace(
            '#endif\n\n#ifdef Q_OS_WIN\n',
            '''static QRect
athenaVariantMapToRect(const QVariantMap& map)
{
    return QRect(
        QPoint(qRound(map.value(QStringLiteral("x")).toDouble()),
               qRound(map.value(QStringLiteral("y")).toDouble())),
        QSize(qRound(map.value(QStringLiteral("width")).toDouble()),
              qRound(map.value(QStringLiteral("height")).toDouble())));
}

static QPoint
athenaVariantMapToPoint(const QVariantMap& map)
{
    return QPoint(qRound(map.value(QStringLiteral("x")).toDouble()),
                  qRound(map.value(QStringLiteral("y")).toDouble()));
}

static bool
athenaKWinDockingAvailable()
{
    static int cachedAvailable = -1;
    if (cachedAvailable >= 0)
    {
        return cachedAvailable == 1;
    }

    if (QApplication::platformName() != QStringLiteral("wayland"))
    {
        cachedAvailable = 0;
        return false;
    }

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"),
                        QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"),
                        QDBusConnection::sessionBus());
    if (!kwin.isValid())
    {
        cachedAvailable = 0;
        return false;
    }

    QDBusReply<QString> ping = kwin.call(QStringLiteral("athenaPing"));
    cachedAvailable = ping.isValid() &&
                      ping.value().startsWith(QStringLiteral("ATHENA modified KWin")) ? 1 : 0;
    return cachedAvailable == 1;
}

static QString
athenaFindKWinWindowId(QWidget* widget)
{
    if (widget == nullptr)
    {
        return QString();
    }

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"),
                        QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"),
                        QDBusConnection::sessionBus());
    QDBusReply<QVariantList> reply = kwin.call(QStringLiteral("athenaListWindows"));
    if (!reply.isValid())
    {
        return QString();
    }

    const QString title = widget->windowTitle();
    const QRect frame = widget->frameGeometry();
    const QPoint frameCenter = frame.center();
    QString bestId;
    int bestScore = INT_MAX;

    const QVariantList windows = reply.value();
    for (const QVariant& item : windows)
    {
        const QVariantMap window = item.toMap();
        const QString id = window.value(QStringLiteral("windowId")).toString();
        if (id.isEmpty())
        {
            continue;
        }

        const QString caption = window.value(QStringLiteral("caption")).toString();
        const QRect candidateFrame = athenaVariantMapToRect(
            window.value(QStringLiteral("frameGeometry")).toMap());
        if (!title.isEmpty() && !caption.isEmpty() && caption != title)
        {
            const QPoint cursor = QCursor::pos();
            if (!candidateFrame.contains(cursor))
            {
                continue;
            }
        }

        const QPoint delta = candidateFrame.center() - frameCenter;
        int score = qAbs(delta.x()) + qAbs(delta.y())
                  + qAbs(candidateFrame.width() - frame.width())
                  + qAbs(candidateFrame.height() - frame.height());
        if (caption == title)
        {
            score -= 100000;
        }
        if (candidateFrame.contains(QCursor::pos()))
        {
            score -= 10000;
        }
        if (score < bestScore)
        {
            bestScore = score;
            bestId = id;
        }
    }

    return bestId;
}
#endif

#ifdef Q_OS_WIN
''')
    content = content.replace(
        'if (qgetenv("XDG_SESSION_TYPE").toLower() != "wayland")',
        'if (QApplication::platformName() != QStringLiteral("wayland"))')
    content = content.replace(
        '''athenaKWinDockingAvailable()
{
    if (QApplication::platformName() != QStringLiteral("wayland"))
    {
        return false;
    }
''',
        '''athenaKWinDockingAvailable()
{
    static int cachedAvailable = -1;
    if (cachedAvailable >= 0)
    {
        return cachedAvailable == 1;
    }

    if (QApplication::platformName() != QStringLiteral("wayland"))
    {
        cachedAvailable = 0;
        return false;
    }
''')
    content = content.replace(
        '''    if (!kwin.isValid())
    {
        return false;
    }

    QDBusReply<QString> ping = kwin.call(QStringLiteral("athenaPing"));
    return ping.isValid() && ping.value().startsWith(QStringLiteral("ATHENA modified KWin"));
}
''',
        '''    if (!kwin.isValid())
    {
        cachedAvailable = 0;
        return false;
    }

    QDBusReply<QString> ping = kwin.call(QStringLiteral("athenaPing"));
    cachedAvailable = ping.isValid() &&
                      ping.value().startsWith(QStringLiteral("ATHENA modified KWin")) ? 1 : 0;
    return cachedAvailable == 1;
}
''')

    if 'QString AthenaKWinDockDragId' not in content:
        new_content = content.replace(
            '''\tbool IsResizing = false;
    bool MousePressed = false;
#endif
''',
            '''\tbool IsResizing = false;
    bool MousePressed = false;
    QString AthenaKWinDockDragId;
    QString AthenaKWinDockCallbackPath;
#endif
''')
        if new_content == content:
            new_content = content.replace(
                '''    bool IsResizing = false;
    bool MousePressed = false;
#endif
''',
                '''    bool IsResizing = false;
    bool MousePressed = false;
    QString AthenaKWinDockDragId;
    QString AthenaKWinDockCallbackPath;
#endif
''')
        content = new_content

    if 'athenaTryStartKWinDockDrag' not in content:
        content = content.replace(
            '''void CFloatingDockContainer::startFloating(const QPoint &DragStartMousePos,
    const QSize &Size, eDragState DragState, QWidget *MouseEventHandler)
''',
            '''bool CFloatingDockContainer::athenaKWinDockDragActive() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return !d->AthenaKWinDockDragId.isEmpty();
#else
    return false;
#endif
}

bool CFloatingDockContainer::athenaTryStartKWinDockDrag(const QPoint& hotSpot)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (!athenaKWinDockingAvailable())
    {
        return false;
    }

    const QString windowId = athenaFindKWinWindowId(this);
    if (windowId.isEmpty())
    {
        return false;
    }

    const QString objectPath = QStringLiteral("/org/athena/ADS/FloatingDockContainer/%1")
        .arg(reinterpret_cast<quintptr>(this), 0, 16);
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(objectPath);
    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAllSlots))
    {
        return false;
    }

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"),
                        QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"),
                        bus);
    QDBusReply<QString> reply = kwin.call(QStringLiteral("athenaBeginDockDrag"),
                                          windowId,
                                          objectPath,
                                          hotSpot.x(),
                                          hotSpot.y());
    if (!reply.isValid() || reply.value().isEmpty())
    {
        bus.unregisterObject(objectPath);
        return false;
    }

    d->AthenaKWinDockDragId = reply.value();
    d->AthenaKWinDockCallbackPath = objectPath;
    return true;
#else
    Q_UNUSED(hotSpot)
    return false;
#endif
}

void CFloatingDockContainer::athenaFinishKWinDockDrag(const QVariantMap& state, bool dropped)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    const QString dragId = state.value(QStringLiteral("dragId")).toString();
    if (d->AthenaKWinDockDragId.isEmpty() || dragId != d->AthenaKWinDockDragId)
    {
        return;
    }

    const QPoint globalPos = athenaVariantMapToPoint(
        state.value(QStringLiteral("pointerGlobal")).toMap());
    if (!globalPos.isNull())
    {
        d->updateDropOverlays(globalPos);
    }

    const QString objectPath = d->AthenaKWinDockCallbackPath;
    d->AthenaKWinDockDragId.clear();
    d->AthenaKWinDockCallbackPath.clear();
    if (!objectPath.isEmpty())
    {
        QDBusConnection::sessionBus().unregisterObject(objectPath);
    }

    if (dropped)
    {
        d->titleMouseReleaseEvent();
    }
    else
    {
        d->handleEscapeKey();
    }
#else
    Q_UNUSED(state)
    Q_UNUSED(dropped)
#endif
}

void CFloatingDockContainer::dockDragMoved(const QVariantMap& state)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (state.value(QStringLiteral("dragId")).toString() != d->AthenaKWinDockDragId)
    {
        return;
    }
    const QPoint globalPos = athenaVariantMapToPoint(
        state.value(QStringLiteral("pointerGlobal")).toMap());
    if (!globalPos.isNull())
    {
        d->updateDropOverlays(globalPos);
    }
#else
    Q_UNUSED(state)
#endif
}

void CFloatingDockContainer::dockDragDropped(const QVariantMap& state)
{
    athenaFinishKWinDockDrag(state, true);
}

void CFloatingDockContainer::dockDragCancelled(const QVariantMap& state)
{
    athenaFinishKWinDockDrag(state, false);
}

void CFloatingDockContainer::startFloating(const QPoint &DragStartMousePos,
    const QSize &Size, eDragState DragState, QWidget *MouseEventHandler)
''')

    content = content.replace(
        '''\td->setState(DragState);
\tif (DraggingFloatingWidget == DragState)
\t{
\t\td->MouseEventHandler = MouseEventHandler;
\t\tif (d->MouseEventHandler)
\t\t{
\t\t\td->MouseEventHandler->grabMouse();
\t\t}
\t}

\tif (!isMaximized())
\t{
\t\tmoveFloating();
\t}
\tshow();
''',
        '''\td->setState(DragState);
\tif (DraggingFloatingWidget == DragState)
\t{
\t\tshow();
\t\tif (athenaTryStartKWinDockDrag(DragStartMousePos))
\t\t{
\t\t\treturn;
\t\t}
\t\td->MouseEventHandler = MouseEventHandler;
\t\tif (d->MouseEventHandler)
\t\t{
\t\t\td->MouseEventHandler->grabMouse();
\t\t}
\t}

\tif (!isMaximized())
\t{
\t\tmoveFloating();
\t}
\tshow();
''')

    content = content.replace(
        '''void CFloatingDockContainer::moveFloating()
{
\tint BorderSize = (frameSize().width() - size().width()) / 2;
''',
        '''void CFloatingDockContainer::moveFloating()
{
\tif (athenaKWinDockDragActive())
\t{
\t\treturn;
\t}
\tint BorderSize = (frameSize().width() - size().width()) / 2;
''')

    content = content.replace(
        '''void CFloatingDockContainer::finishDragging()
{
\tADS_PRINT("CFloatingDockContainer::finishDragging");
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tsetWindowOpacity(1);
\tactivateWindow();
\tif (d->MouseEventHandler)
\t{
\t   d->MouseEventHandler->releaseMouse();
\t   d->MouseEventHandler = nullptr;
\t}
#endif
\td->titleMouseReleaseEvent();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tathenaSnapFloatingContainerToScreenEdge(this);
#endif
}
''',
        '''void CFloatingDockContainer::finishDragging()
{
\tADS_PRINT("CFloatingDockContainer::finishDragging");
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tsetWindowOpacity(1);
\tactivateWindow();
\tif (d->MouseEventHandler)
\t{
\t   d->MouseEventHandler->releaseMouse();
\t   d->MouseEventHandler = nullptr;
\t}
\tif (athenaKWinDockDragActive())
\t{
\t\treturn;
\t}
#endif
\td->titleMouseReleaseEvent();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tathenaSnapFloatingContainerToScreenEdge(this);
#endif
}
''')

    if content != original_content:
        with open(cpp_path, 'w') as f:
            f.write(content)
        print(f"Patched {cpp_path}")
    else:
        print(f"No changes needed for {cpp_path}")

# If run from CMake, the first argument is the source directory and the second
# one is the Qt major version used for this build.
base_dir = sys.argv[1] if len(sys.argv) > 1 else "."
qt_major = sys.argv[2] if len(sys.argv) > 2 else "5"

if qt_major != "6":
    # Qt5 HiDPI handling undersizes ADS controls on ATHENA's target desktops.
    # Qt6 scales these controls correctly; inflating them there makes window
    # controls visibly oversized.
    cpp_files = [
        'src/DockAreaTitleBar.cpp',
        'src/DockManager.cpp',
        'src/DockWidget.cpp'
    ]

    for cpp in cpp_files:
        path = os.path.join(base_dir, cpp)
        patch_file(path, ['QSize(16, 16)', 'QSize(24, 24)'], 'QSize(32, 32)')

    css_files = [
        'src/stylesheets/default.css',
        'src/stylesheets/default_linux.css',
        'src/stylesheets/default_windows.css'
    ]

    for css in css_files:
        path = os.path.join(base_dir, css)
        patch_file(path, ['qproperty-iconSize: 16px;', 'qproperty-iconSize: 24px;'], 'qproperty-iconSize: 32px;')
        patch_file(path, ['qproperty-iconSize: 16px 16px;', 'qproperty-iconSize: 24px 24px;'], 'qproperty-iconSize: 32px 32px;')
else:
    print("Skipping ADS HiDPI size inflation for Qt6")

patch_ads_floating_windows(base_dir)
patch_ads_qt6_private_gui(base_dir)
patch_ads_kwin_wayland_docking(base_dir)
