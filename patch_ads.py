import os
import re
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


def write_if_changed(path, content, original_content):
    if content != original_content:
        with open(path, 'w') as f:
            f.write(content)
        print(f"Patched {path}")
    else:
        print(f"No changes needed for {path}")


def patch_ads_floating_header(base_dir):
    path = os.path.join(base_dir, 'src/FloatingDockContainer.h')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    content = content.replace('#include <QRubberBand>\n#include <QVariantMap>\n',
                              '#include <QRubberBand>\n')
    if 'class QObject;\nclass QEvent;\n' not in content:
        content = content.replace('#include <QRubberBand>\n',
                                  '#include <QRubberBand>\n\nclass QObject;\nclass QEvent;\n')
    content = content.replace('class AthenaAdsWaylandDockDragFilter;\n\nnamespace ads\n',
                              'namespace ads\n')
    if 'class CFloatingWidgetTitleBar;\nclass CDockingStateReader;\nclass AthenaAdsWaylandDockDragFilter;\n' not in content:
        content = content.replace('class CFloatingWidgetTitleBar;\nclass CDockingStateReader;\n',
                                  'class CFloatingWidgetTitleBar;\nclass CDockingStateReader;\nclass AthenaAdsWaylandDockDragFilter;\n',
                                  1)

    content = content.replace('\tQ_CLASSINFO("D-Bus Interface", "org.athena.KWinDockDragSink")\n', '')
    content = re.sub(
        r'\n\tbool athenaTryStartKWinDockDrag\(const QPoint& hotSpot\);\n'
        r'\tbool athenaKWinDockDragActive\(\) const;\n'
        r'\tvoid athenaFinishKWinDockDrag\(const QVariantMap& state, bool dropped\);\n',
        '\n\tbool athenaTryStartWaylandDockDrag(const QPoint& hotSpot, QWidget* sourceWidget);\n'
        '\tbool athenaWaylandDockDragActive() const;\n'
        '\tbool athenaWaylandDockDragStarted() const;\n'
        '\tvoid athenaUpdateWaylandDockDrag(const QPoint& globalPos);\n'
        '\tbool athenaHasWaylandDockTarget() const;\n'
        '\tbool athenaFinishWaylandDockDrag(bool dropped);\n'
        '\tvoid athenaHideWaylandDockOverlays();\n',
        content)
    content = content.replace('bool athenaTryStartWaylandDockDrag(const QPoint& hotSpot);',
                              'bool athenaTryStartWaylandDockDrag(const QPoint& hotSpot, QWidget* sourceWidget);')
    if 'athenaTryStartWaylandDockDrag' not in content:
        content = content.replace(
            'private:\n\tFloatingDockContainerPrivate* d; ///< private data (pimpl)\n',
            'private:\n\tFloatingDockContainerPrivate* d; ///< private data (pimpl)\n'
            '\tbool athenaTryStartWaylandDockDrag(const QPoint& hotSpot, QWidget* sourceWidget);\n'
            '\tbool athenaWaylandDockDragActive() const;\n'
            '\tbool athenaWaylandDockDragStarted() const;\n'
            '\tvoid athenaUpdateWaylandDockDrag(const QPoint& globalPos);\n'
            '\tbool athenaHasWaylandDockTarget() const;\n'
            '\tbool athenaFinishWaylandDockDrag(bool dropped);\n'
            '\tvoid athenaHideWaylandDockOverlays();\n',
            1)
    if ('bool athenaWaylandDockDragActive() const;\n'
            '\tbool athenaWaylandDockDragStarted() const;') not in content:
        content = content.replace('bool athenaWaylandDockDragActive() const;\n',
                                  'bool athenaWaylandDockDragActive() const;\n'
                                  '\tbool athenaWaylandDockDragStarted() const;\n')

    content = re.sub(
        r'\npublic Q_SLOTS:\n'
        r'\tvoid dockDragMoved\(const QVariantMap& state\);\n'
        r'\tvoid dockDragDropped\(const QVariantMap& state\);\n'
        r'\tvoid dockDragCancelled\(const QVariantMap& state\);\n',
        '\n',
        content)
    if 'friend class AthenaAdsWaylandDockDragFilter;' not in content:
        content = content.replace('    friend class CFloatingWidgetTitleBar;\n',
                                  '    friend class CFloatingWidgetTitleBar;\n'
                                  '\tfriend class AthenaAdsWaylandDockDragFilter;\n',
                                  1)

    write_if_changed(path, content, original_content)


WAYLAND_INITIAL_DOCK_DRAG_HELPER = '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static bool
athenaUseWaylandToplevelDragForInitialFloating()
{
\treturn QApplication::platformName().startsWith(QStringLiteral("wayland"));
}
#endif

'''


def patch_ads_initial_wayland_drag(base_dir):
    tab_path = os.path.join(base_dir, 'src/DockWidgetTab.cpp')
    if os.path.exists(tab_path):
        with open(tab_path, 'r') as f:
            content = f.read()

        original_content = content
        if '#include <QPointer>' not in content:
            content = content.replace('#include <QMenu>\n',
                                      '#include <QMenu>\n#include <QPointer>\n')
        content = content.replace(
            'TitleLabel->setObjectName("dockWidgetTabLabel");\n'
            '\tTitleLabel->setAlignment(Qt::AlignCenter);\n',
            'TitleLabel->setObjectName("dockWidgetTabLabel");\n'
            '#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)\n'
            '\tif (QApplication::platformName().startsWith(QStringLiteral("wayland")))\n'
            '\t{\n'
            '\t\tTitleLabel->setFont(qApp->font());\n'
            '\t\t_this->setFont(qApp->font());\n'
            '\t}\n'
            '#endif\n'
            '\tTitleLabel->setAlignment(Qt::AlignCenter);\n')
        if 'athenaUseWaylandToplevelDragForInitialFloating' not in content:
            content = content.replace('namespace ads\n{\n',
                                      'namespace ads\n{\n' + WAYLAND_INITIAL_DOCK_DRAG_HELPER,
                                      1)
        tab_create_container_block = '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tbool UseWaylandToplevelDrag = false;
\tif (!CreateContainer && athenaUseWaylandToplevelDragForInitialFloating())
\t{
\t\tCreateContainer = true;
\t\tUseWaylandToplevelDrag = true;
\t}
#else
\tconst bool UseWaylandToplevelDrag = false;
#endif
'''
        content = re.sub(
            r'\tbool CreateContainer = \(DraggingFloatingWidget != DraggingState\);\n'
            r'(?:(?:#if defined\(Q_OS_UNIX\) && !defined\(Q_OS_MACOS\)'
            r'(?: && \(QT_VERSION >= QT_VERSION_CHECK\(6, 0, 0\)\))?\n'
            r'\tbool UseWaylandToplevelDrag = false;\n'
            r'\tif \(!CreateContainer && athenaUseWaylandToplevelDragForInitialFloating\(\)\)\n'
            r'\t\{\n'
            r'\t\tCreateContainer = true;\n'
            r'\t\tUseWaylandToplevelDrag = true;\n'
            r'\t\}\n'
            r'#else\n'
            r'\tconst bool UseWaylandToplevelDrag = false;\n'
            r'#endif\n)+)?',
            '\tbool CreateContainer = (DraggingFloatingWidget != DraggingState);\n'
            + tab_create_container_block,
            content,
            count=1)
        content = content.replace(
            '''    if (DraggingFloatingWidget == DraggingState)
    {
        FloatingWidget->startFloating(DragStartMousePosition, Size, DraggingFloatingWidget, _this);
        auto DockManager = DockWidget->dockManager();
''',
            '''    if (DraggingFloatingWidget == DraggingState)
    {
\t\tQPointer<CFloatingDockContainer> WaylandFloatingDockContainer =
\t\t\tdynamic_cast<CFloatingDockContainer*>(FloatingWidget);
        FloatingWidget->startFloating(DragStartMousePosition, Size, DraggingFloatingWidget, _this);
\t\tif (UseWaylandToplevelDrag
\t\t    && (!WaylandFloatingDockContainer
\t\t        || WaylandFloatingDockContainer->athenaWaylandDockDragStarted()))
\t\t{
\t\t\tDragState = DraggingInactive;
\t\t\tthis->FloatingWidget = nullptr;
\t\t\treturn true;
\t\t}
        auto DockManager = DockWidget->dockManager();
''',
            1)
        if 'A single dock in a floating container is already floating.' not in content:
            content = re.sub(
                r'\t// if this is the last dock widget inside of this floating widget,\n'
                r'\t// then it does not make any sense, to make it floating because\n'
                r'\t// it is already floating\n'
                r'\t if \(dockContainer->isFloating\(\)\n'
                r'\t && \(dockContainer->visibleDockAreaCount\(\) == 1\)\n'
                r'\t && \(DockWidget->dockAreaWidget\(\)->dockWidgetsCount\(\) == 1\)\)\n'
                r'\t\{\n'
                r'\t\treturn false;\n'
                r'\t\}\n',
                '''\t// A single dock in a floating container is already floating. On
\t// Wayland it can still start an xdg-toplevel-drag redocking session
\t// from the app-owned tab, while the system title bar remains a normal
\t// window move handle.
\t if (dockContainer->isFloating()
\t && (dockContainer->visibleDockAreaCount() == 1)
\t && (DockWidget->dockAreaWidget()->dockWidgetsCount() == 1))
\t{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\t\tif (athenaUseWaylandToplevelDragForInitialFloating())
\t\t{
\t\t\tif (auto* FloatingContainer = dockContainer->floatingWidget())
\t\t\t{
\t\t\t\tQPoint HotSpot = FloatingContainer->mapFromGlobal(GlobalDragStartMousePosition);
\t\t\t\tFloatingContainer->startDragging(HotSpot, FloatingContainer->size(), _this);
\t\t\t\tDragState = DraggingInactive;
\t\t\t\treturn FloatingContainer->athenaWaylandDockDragStarted();
\t\t\t}
\t\t}
#endif
\t\treturn false;
\t}\n''',
                content,
                count=1)
        if 'A single dock tab in a floating container is already floating.' not in content:
            content = re.sub(
                r'\t\t// If this is the last dock area in a dock container with only\n'
                r'\s*// one single dock widget it does not make  sense to move it to a new\n'
                r'\s*// floating widget and leave this one empty\n'
                r'\t\tif \(d->DockArea->dockContainer\(\)->isFloating\(\)\n'
                r'\t\t && d->DockArea->openDockWidgetsCount\(\) == 1\n'
                r'\t\t && d->DockArea->dockContainer\(\)->visibleDockAreaCount\(\) == 1\)\n'
                r'\t\t\{\n'
                r'\t\t\treturn;\n'
                r'\t\t\}\n',
                '''		// A single dock tab in a floating container is already floating.
		// On Wayland, dragging the app-owned tab is the redocking gesture.
		if (d->DockArea->dockContainer()->isFloating()
		 && d->DockArea->openDockWidgetsCount() == 1
		 && d->DockArea->dockContainer()->visibleDockAreaCount() == 1)
		{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
			if (athenaUseWaylandToplevelDragForInitialFloating())
			{
				if (auto* FloatingContainer = d->DockArea->dockContainer()->floatingWidget())
				{
					QPoint HotSpot = FloatingContainer->mapFromGlobal(d->GlobalDragStartMousePosition);
					FloatingContainer->startDragging(HotSpot, FloatingContainer->size(), this);
					d->DragState = DraggingInactive;
				}
			}
#endif
			return;
		}
''',
                content,
                count=1)
        write_if_changed(tab_path, content, original_content)
    else:
        print(f"File not found: {tab_path}")

    title_path = os.path.join(base_dir, 'src/DockAreaTitleBar.cpp')
    if os.path.exists(title_path):
        with open(title_path, 'r') as f:
            content = f.read()

        original_content = content
        if 'athenaUseWaylandToplevelDragForInitialFloating' not in content:
            content = content.replace('namespace ads\n{\n',
                                      'namespace ads\n{\n' + WAYLAND_INITIAL_DOCK_DRAG_HELPER,
                                      1)
        title_create_container_block = '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tbool UseWaylandToplevelDrag = false;
\tif (!CreateFloatingDockContainer && athenaUseWaylandToplevelDragForInitialFloating())
\t{
\t\tCreateFloatingDockContainer = true;
\t\tUseWaylandToplevelDrag = true;
\t}
#else
\tconst bool UseWaylandToplevelDrag = false;
#endif
'''
        content = re.sub(
            r'\tbool CreateFloatingDockContainer = \(DraggingFloatingWidget != DragState\);\n'
            r'(?:(?:#if defined\(Q_OS_UNIX\) && !defined\(Q_OS_MACOS\)'
            r'(?: && \(QT_VERSION >= QT_VERSION_CHECK\(6, 0, 0\)\))?\n'
            r'\tbool UseWaylandToplevelDrag = false;\n'
            r'\tif \(!CreateFloatingDockContainer && athenaUseWaylandToplevelDragForInitialFloating\(\)\)\n'
            r'\t\{\n'
            r'\t\tCreateFloatingDockContainer = true;\n'
            r'\t\tUseWaylandToplevelDrag = true;\n'
            r'\t\}\n'
            r'#else\n'
            r'\tconst bool UseWaylandToplevelDrag = false;\n'
            r'#endif\n)+)?',
            '\tbool CreateFloatingDockContainer = (DraggingFloatingWidget != DragState);\n'
            + title_create_container_block,
            content,
            count=1)
        title_start_block = '''\tQPointer<CFloatingDockContainer> WaylandFloatingDockContainer = FloatingDockContainer;
    FloatingWidget->startFloating(Offset, Size, DragState, nullptr);
\tif (UseWaylandToplevelDrag
\t    && (!WaylandFloatingDockContainer
\t        || WaylandFloatingDockContainer->athenaWaylandDockDragStarted()))
\t{
\t\tthis->DragState = DraggingInactive;
\t\treturn nullptr;
\t}
    if (FloatingDockContainer)
    {
'''
        content = re.sub(
            r'(?:\tQPointer<CFloatingDockContainer> WaylandFloatingDockContainer = FloatingDockContainer;\n)?'
            r'\s*FloatingWidget->startFloating\(Offset, Size, DragState, nullptr\);\n'
            r'.*?^\s*if \(FloatingDockContainer\)\n\s*\{\n',
            title_start_block,
            content,
            count=1,
            flags=re.S | re.M)
        content = content.replace(
            '''\tFloatingWidget = makeAreaFloating(Offset, DraggingFloatingWidget);
\tqApp->postEvent(DockArea, new QEvent((QEvent::Type)internal::DockedWidgetDragStartEvent));
''',
            '''\tFloatingWidget = makeAreaFloating(Offset, DraggingFloatingWidget);
\tif (FloatingWidget)
\t{
\t\tqApp->postEvent(DockArea, new QEvent((QEvent::Type)internal::DockedWidgetDragStartEvent));
\t}
''',
            1)
        if 'A single dock area in a floating container is already floating.' not in content:
            content = re.sub(
                r'\t// If this is the last dock area in a floating dock container it does not make\n'
                r'\t// sense to move it to a new floating widget and leave this one\n'
                r'\t// empty\n'
                r'\tif \(d->DockArea->dockContainer\(\)->isFloating\(\)\n'
                r'\t && d->DockArea->dockContainer\(\)->visibleDockAreaCount\(\) == 1 \n'
                r'     && !d->DockArea->isAutoHide\(\)\)\n'
                r'\t\{\n'
                r'\t\treturn;\n'
                r'\t\}\n',
                '''\t// A single dock area in a floating container is already floating.
\t// On Wayland, use the app-owned area title bar as the redocking drag
\t// handle and keep the compositor title bar for normal window movement.
\tif (d->DockArea->dockContainer()->isFloating()
\t && d->DockArea->dockContainer()->visibleDockAreaCount() == 1
     && !d->DockArea->isAutoHide())
\t{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\t\tif (athenaUseWaylandToplevelDragForInitialFloating())
\t\t{
\t\t\tif (auto* FloatingContainer = d->DockArea->dockContainer()->floatingWidget())
\t\t\t{
\t\t\t\tQPoint GlobalPressPos = mapToGlobal(d->DragStartMousePos);
\t\t\t\tQPoint HotSpot = FloatingContainer->mapFromGlobal(GlobalPressPos);
\t\t\t\tFloatingContainer->startDragging(HotSpot, FloatingContainer->size(), this);
\t\t\t\td->DragState = DraggingInactive;
\t\t\t\td->FloatingWidget = nullptr;
\t\t\t\treturn;
\t\t\t}
\t\t}
#endif
\t\treturn;
\t}\n''',
                content,
                count=1)
        content = content.replace(
            '\t && d->DockArea->dockContainer()->visibleDockAreaCount() == 1 \n',
            '\t && d->DockArea->dockContainer()->visibleDockAreaCount() == 1\n')
        write_if_changed(title_path, content, original_content)
    else:
        print(f"File not found: {title_path}")


def patch_ads_wayland_single_floating_tabbar(base_dir):
    path = os.path.join(base_dir, 'src/DockAreaWidget.cpp')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    if '#include <QApplication>' not in content:
        content = content.replace('#include <QList>\n',
                                  '#include <QList>\n#include <QApplication>\n',
                                  1)

    helper = '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static bool
athenaUseWaylandSingleFloatingTabBar()
{
\treturn QApplication::platformName().startsWith(QStringLiteral("wayland"));
}
#endif

'''
    if 'athenaUseWaylandSingleFloatingTabBar' not in content:
        content = content.replace('namespace ads\n{\n',
                                  'namespace ads\n{\n' + helper,
                                  1)

    content = content.replace(
        '''\t\tbool Hidden = Container->hasTopLevelDockWidget() && (Container->isFloating()
\t\t\t|| CDockManager::testConfigFlag(CDockManager::HideSingleCentralWidgetTitleBar));
\t\tHidden |= (d->Flags.testFlag(HideSingleWidgetTitleBar) && openDockWidgetsCount() == 1);
\t\tHidden &= !IsAutoHide; // Titlebar must always be visible when auto hidden so it can be dragged
\t\td->TitleBar->setVisible(!Hidden);
''',
        '''\t\tbool ForceWaylandFloatingTitleBar = false;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\t\tForceWaylandFloatingTitleBar =
\t\t\tContainer->isFloating() && athenaUseWaylandSingleFloatingTabBar();
#endif
\t\tbool Hidden = Container->hasTopLevelDockWidget() && (Container->isFloating()
\t\t\t|| CDockManager::testConfigFlag(CDockManager::HideSingleCentralWidgetTitleBar));
\t\tHidden |= (d->Flags.testFlag(HideSingleWidgetTitleBar) && openDockWidgetsCount() == 1);
\t\tif (ForceWaylandFloatingTitleBar)
\t\t{
\t\t\tHidden = false;
\t\t}
\t\tHidden &= !IsAutoHide; // Titlebar must always be visible when auto hidden so it can be dragged
\t\td->TitleBar->setVisible(!Hidden);
''',
        1)

    write_if_changed(path, content, original_content)


def patch_ads_wayland_overlay_position(base_dir):
    path = os.path.join(base_dir, 'src/DockOverlay.cpp')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    helper = '''static const int InvalidTabIndex = -2;

static bool AthenaWaylandDockOverlayPositionActive = false;
static QPoint AthenaWaylandDockOverlayPosition;

void athenaSetWaylandDockOverlayCursorPosition(const QPoint& globalPos)
{
\tAthenaWaylandDockOverlayPosition = globalPos;
\tAthenaWaylandDockOverlayPositionActive = true;
}

void athenaClearWaylandDockOverlayCursorPosition()
{
\tAthenaWaylandDockOverlayPositionActive = false;
}

static QPoint athenaDockOverlayCursorPosition()
{
\treturn AthenaWaylandDockOverlayPositionActive
\t\t? AthenaWaylandDockOverlayPosition
\t\t: QCursor::pos();
}
'''
    if 'athenaSetWaylandDockOverlayCursorPosition' not in content:
        content = content.replace('static const int InvalidTabIndex = -2;\n',
                                  helper,
                                  1)

    edge_helper = '''static DockWidgetArea
athenaWaylandContainerEdgeArea(const DockOverlayPrivate* overlay)
{
\tif (!AthenaWaylandDockOverlayPositionActive
\t    || overlay == nullptr
\t    || overlay->Mode != CDockOverlay::ModeContainerOverlay)
\t{
\t\treturn InvalidDockWidgetArea;
\t}

\tQWidget* target = overlay->TargetWidget.data();
\tauto* container = qobject_cast<CDockContainerWidget*>(target);
\tif (target == nullptr || container == nullptr)
\t{
\t\treturn InvalidDockWidgetArea;
\t}

\tconst QRect rect = target->rect();
\tconst QPoint pos = target->mapFromGlobal(athenaDockOverlayCursorPosition());
\tif (!rect.contains(pos))
\t{
\t\treturn InvalidDockWidgetArea;
\t}

\tconst int marginX = qMin(qMax(rect.width() / 8, 48), 160);
\tconst int marginY = qMin(qMax(rect.height() / 8, 48), 160);
\tDockWidgetArea bestArea = InvalidDockWidgetArea;
\tint bestDistance = 1 << 30;
\tauto consider = [&](DockWidgetArea area, int distance, int margin) {
\t\tif (distance <= margin
\t\t    && distance < bestDistance
\t\t    && overlay->AllowedAreas.testFlag(area))
\t\t{
\t\t\tbestArea = area;
\t\t\tbestDistance = distance;
\t\t}
\t};

\tconsider(LeftDockWidgetArea, pos.x() - rect.left(), marginX);
\tconsider(RightDockWidgetArea, rect.right() - pos.x(), marginX);
\tconsider(TopDockWidgetArea, pos.y() - rect.top(), marginY);
\tconsider(BottomDockWidgetArea, rect.bottom() - pos.y(), marginY);
\treturn bestArea;
}

'''
    if 'static DockWidgetArea\nathenaWaylandContainerEdgeArea' not in content:
        content = content.replace('};\n\n\t/**\n\t * Private data of CDockOverlayCross class',
                                  '};\n\n' + edge_helper
                                  + '\t/**\n\t * Private data of CDockOverlayCross class',
                                  1)
        content = content.replace('};\n\n/**\n * Private data of CDockOverlayCross class',
                                  '};\n\n' + edge_helper
                                  + '/**\n * Private data of CDockOverlayCross class',
                                  1)
    content = content.replace(
        '''\tQWidget* target = overlay->TargetWidget.data();
\tif (target == nullptr || qobject_cast<CDockContainerWidget*>(target) == nullptr)
\t{
\t\treturn InvalidDockWidgetArea;
\t}
''',
'''\tQWidget* target = overlay->TargetWidget.data();
\tauto* container = qobject_cast<CDockContainerWidget*>(target);
\tif (target == nullptr || container == nullptr)
\t{
\t\treturn InvalidDockWidgetArea;
\t}
''')
    content = content.replace(
        '''\tQWidget* target = overlay->TargetWidget.data();
\tauto* container = qobject_cast<CDockContainerWidget*>(target);
\tif (target == nullptr || container == nullptr || container->isFloating())
\t{
\t\treturn InvalidDockWidgetArea;
\t}
''',
        '''\tQWidget* target = overlay->TargetWidget.data();
\tauto* container = qobject_cast<CDockContainerWidget*>(target);
\tif (target == nullptr || container == nullptr)
\t{
\t\treturn InvalidDockWidgetArea;
\t}
''')

    content = content.replace('auto CursorPos = QCursor::pos();',
                              'auto CursorPos = athenaDockOverlayCursorPosition();')
    content = content.replace('const QPoint pos = mapFromGlobal(QCursor::pos());',
                              'const QPoint pos = mapFromGlobal(athenaDockOverlayCursorPosition());')

    if 'Result = athenaWaylandContainerEdgeArea(d);' not in content:
        content = content.replace(
            '''\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}

\tauto CursorPos = athenaDockOverlayCursorPosition();
''',
            '''\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}

\tResult = athenaWaylandContainerEdgeArea(d);
\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}

\tauto CursorPos = athenaDockOverlayCursorPosition();
''',
            1)
    content = content.replace(
        '''\tResult = athenaWaylandContainerEdgeArea(d);
\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}

\tResult = athenaWaylandContainerEdgeArea(d);
\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}
''',
        '''\tResult = athenaWaylandContainerEdgeArea(d);
\tif (Result != InvalidDockWidgetArea)
\t{
\t\treturn Result;
\t}
''')

    write_if_changed(path, content, original_content)


def patch_ads_floating_windows(base_dir):
    # ATHENA keeps floating docks as normal desktop windows. Redocking on native
    # Wayland is driven by app-owned ADS tab/title-bar drags through Qt's
    # xdg-toplevel-drag path.
    patch_ads_floating_header(base_dir)

    path = os.path.join(base_dir, 'src/FloatingDockContainer.cpp')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content

    for include in ('#include <QDBusConnection>\n',
                    '#include <QDBusInterface>\n',
                    '#include <QDBusReply>\n'):
        content = content.replace(include, '')
    if '#include <QScreen>' not in content:
        content = content.replace('#include <QTime>\n',
                                  '#include <QTime>\n#include <QScreen>\n')
    for include in [
        '#include <QByteArray>',
        '#include <QDataStream>',
        '#include <QDrag>',
        '#include <QDragEnterEvent>',
        '#include <QDragLeaveEvent>',
        '#include <QDropEvent>',
        '#include <QMimeData>',
        '#include <QTimer>',
        '#include <QVector>',
        '#include <QWindow>',
        '#include <fstream>',
        '#include <iostream>',
        '#include <sstream>',
        '#include <string>',
    ]:
        if include not in content:
            content = content.replace('#include <QScreen>\n',
                                      '#include <QScreen>\n' + include + '\n',
                                      1)

    content = content.replace('CDockContainerWidget *DropContainer = nullptr;',
                              'QPointer<CDockContainerWidget> DropContainer;\n'
                              '\t\tQPoint AthenaWaylandLastGlobalPos;\n'
                              '\t\tbool AthenaWaylandHasLastGlobalPos = false;')
    content = content.replace('QPointer<CDockContainerWidget> DropContainer;\n\t\tCDockAreaWidget *SingleDockArea',
                              'QPointer<CDockContainerWidget> DropContainer;\n'
                              '\t\tQPoint AthenaWaylandLastGlobalPos;\n'
                              '\t\tbool AthenaWaylandHasLastGlobalPos = false;\n'
                              '\t\tCDockAreaWidget *SingleDockArea')
    content = content.replace('QPointer<CDockContainerWidget> DropContainer;\n\tCDockAreaWidget *SingleDockArea',
                              'QPointer<CDockContainerWidget> DropContainer;\n'
                              '\tQPoint AthenaWaylandLastGlobalPos;\n'
                              '\tbool AthenaWaylandHasLastGlobalPos = false;\n'
                              '\tCDockAreaWidget *SingleDockArea')
    content = content.replace(
        '''\t\tif (DockContainer == ContainerWidget)
\t\t{
\t\t\tcontinue;
\t\t}

\t\tif (athenaIsNativeWaylandPlatform() && ContainerWidget->isFloating())
\t\t{
\t\t\tcontinue;
\t\t}

\t\tQPoint MappedPos = ContainerWidget->mapFromGlobal(GlobalPos);
''',
        '''\t\tif (DockContainer == ContainerWidget)
\t\t{
\t\t\tcontinue;
\t\t}

\t\tQPoint MappedPos = ContainerWidget->mapFromGlobal(GlobalPos);
''',
        1)

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

    if 'athenaFloatingContainerParent' not in content:
        content = content.replace(
            'namespace ads\n{\n',
            '''namespace ads
{
static QWidget*
athenaFloatingContainerParent(CDockManager* dockManager)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tif (QApplication::platformName().startsWith(QStringLiteral("wayland")))
\t{
\t\treturn nullptr;
\t}
#endif
\treturn dockManager;
}

''',
            1)

    content = content.replace(
        '''CFloatingDockContainer::CFloatingDockContainer(CDockManager *DockManager) :
\ttFloatingWidgetBase(DockManager),
''',
        '''CFloatingDockContainer::CFloatingDockContainer(CDockManager *DockManager) :
\ttFloatingWidgetBase(athenaFloatingContainerParent(DockManager)),
''')
    content = content.replace(
        'setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint);',
        'setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint);')

    content = re.sub(
        r'\n\s*if \(native_window\)\n'
        r'\s*\{\n'
        r'\s*// Native windows do not work if wayland is used\..*?'
        r'\n\s*\}\n'
        r'\s*\n(?=\tif \(native_window\))',
        '\n',
        content,
        flags=re.S)

    if 'athenaKWinDockingAvailable' in content:
        content = re.sub(
            r'\nstatic QRect\nathenaVariantMapToRect\(const QVariantMap& map\).*?'
            r'static QString\nathenaFindKWinWindowId\(QWidget\* widget\)\n\{.*?\n\}\n#endif\n',
            '\n#endif\n',
            content,
            flags=re.S)
    content = content.replace(
        '    QString AthenaKWinDockDragId;\n    QString AthenaKWinDockCallbackPath;\n',
        '')

    if 'static const char* const AthenaAdsFloatingDockMime' in content:
        content = re.sub(
            r'#if defined\(Q_OS_UNIX\) && !defined\(Q_OS_MACOS\)'
            r'(?: && \(QT_VERSION >= QT_VERSION_CHECK\(6, 0, 0\)\))?\n'
            r'static const char\* const AthenaAdsFloatingDockMime.*?'
            r'#endif\n\s*static unsigned int zOrderCounterFloating = 0;\n',
            WAYLAND_DOCK_DRAG_HELPERS + '\n\nstatic unsigned int zOrderCounterFloating = 0;\n',
            content,
            flags=re.S)
    else:
        content = content.replace(
            'static unsigned int zOrderCounterFloating = 0;\n',
            WAYLAND_DOCK_DRAG_HELPERS + '\nstatic unsigned int zOrderCounterFloating = 0;\n',
            1)

    if 'bool AthenaWaylandDockDragStarted = false;' not in content:
        content = content.replace('''
\teDragState DraggingState = DraggingInactive;
\tQPoint DragStartMousePosition;
''',
                                  '''
\teDragState DraggingState = DraggingInactive;
\tbool AthenaWaylandDockDragStarted = false;
\tQPoint DragStartMousePosition;
''',
                                  1)

    if 'athenaKWinDockDragActive' in content:
        content = re.sub(
            r'bool CFloatingDockContainer::athenaKWinDockDragActive\(\) const\n\{.*?'
            r'\nvoid CFloatingDockContainer::startFloating',
            WAYLAND_DOCK_DRAG_METHODS + 'void CFloatingDockContainer::startFloating',
            content,
            flags=re.S)
    elif 'athenaWaylandDockDragActive' in content:
        content = re.sub(
            r'bool CFloatingDockContainer::athenaWaylandDockDragActive\(\) const\n\{.*?'
            r'\nvoid CFloatingDockContainer::startFloating',
            WAYLAND_DOCK_DRAG_METHODS + 'void CFloatingDockContainer::startFloating',
            content,
            flags=re.S)
    else:
        content = content.replace('void CFloatingDockContainer::startFloating',
                                  WAYLAND_DOCK_DRAG_METHODS + 'void CFloatingDockContainer::startFloating',
                                  1)

    content = content.replace('athenaTryStartKWinDockDrag', 'athenaTryStartWaylandDockDrag')
    content = content.replace('athenaKWinDockDragActive', 'athenaWaylandDockDragActive')
    content = content.replace('athenaTryStartWaylandDockDrag(DragStartMousePos)',
                              'athenaTryStartWaylandDockDrag(DragStartMousePos, MouseEventHandler)')
    content = content.replace(
        '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\t\t\tif (d->DraggingState == DraggingFloatingWidget)
\t\t\t{
\t\t\t\td->titleMouseReleaseEvent();
\t\t\t\td->DraggingState = DraggingInactive;
\t\t\t}
#endif
''',
        '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\t\t\tif (d->DraggingState == DraggingFloatingWidget
\t\t\t    && !athenaIsNativeWaylandPlatform()
\t\t\t)
\t\t\t{
\t\t\t\td->titleMouseReleaseEvent();
\t\t\t\td->DraggingState = DraggingInactive;
\t\t\t}
#endif
''',
        1)
    content = content.replace(
        '''    if (!d->IsResizing && event->spontaneous() && d->MousePressed)
\t{
        d->setState(DraggingFloatingWidget);
\t\td->updateDropOverlays(QCursor::pos());
\t}
''',
        '''    if (!d->IsResizing && event->spontaneous() && d->MousePressed
        && !athenaIsNativeWaylandPlatform()
        )
\t{
        d->setState(DraggingFloatingWidget);
\t\td->updateDropOverlays(QCursor::pos());
\t}
''',
        1)
    content = content.replace(
        '''\tcase QEvent::WindowDeactivate:
        d->MousePressed = true;
\t\tbreak;
''',
        '''\tcase QEvent::WindowDeactivate:
        d->MousePressed = !athenaIsNativeWaylandPlatform();
\t\tbreak;
''',
        1)
    if 'athenaTryStartWaylandDockDrag(DragStartMousePos, MouseEventHandler)' not in content:
        content = content.replace(
            '''\tif (DraggingFloatingWidget == DragState)
\t{
\t\tshow();
\t\td->MouseEventHandler = MouseEventHandler;
''',
            '''\tif (DraggingFloatingWidget == DragState)
\t{
\t\tshow();
\t\tif (athenaTryStartWaylandDockDrag(DragStartMousePos, MouseEventHandler))
\t\t{
\t\t\treturn;
\t\t}
\t\td->MouseEventHandler = MouseEventHandler;
''',
            1)

    content = content.replace(
        '''void CFloatingDockContainer::show()
{
\t// Prevent this window from showing in the taskbar and pager (alt+tab)
\tinternal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR");
\tinternal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_PAGER");
\tSuper::show();
}
''',
        '''void CFloatingDockContainer::show()
{
\t// These XCB properties are X11-only. Calling winId() here on native
\t// Wayland forces Qt to create native child surfaces while ADS is
\t// reparenting the dock tree.
\tif (QApplication::platformName() == QLatin1String("xcb"))
\t{
\t\tinternal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR");
\t\tinternal::xcb_add_prop(true, winId(), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_PAGER");
\t}
\tSuper::show();
}
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

    write_if_changed(path, content, original_content)


WAYLAND_DOCK_DRAG_HELPERS = '''#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
static const char* const AthenaAdsFloatingDockMime = "application/x-athena-ads-floating-dock";
static const char* const AthenaQtMainWindowDragWindowMime = "application/x-qt-mainwindowdrag-window";
static const char* const AthenaQtMainWindowDragPositionMime = "application/x-qt-mainwindowdrag-position";

static QPointer<CFloatingDockContainer> AthenaActiveWaylandDockDrag;

void athenaSetWaylandDockOverlayCursorPosition(const QPoint& globalPos);
void athenaClearWaylandDockOverlayCursorPosition();

struct AthenaDropTargetState
{
\tQPointer<QWidget> Widget;
\tbool AcceptDrops = false;
};

static QVector<AthenaDropTargetState> AthenaDropTargetStates;

static bool
athenaIsNativeWaylandPlatform()
{
\treturn QApplication::platformName().startsWith(QStringLiteral("wayland"));
}

static bool
athenaAdsWaylandDebugEnabled()
{
\tstatic const bool enabled = qEnvironmentVariableIsSet("ATHENA_SCALE_DEBUG")
\t                         || qEnvironmentVariableIsSet("ATHENA_ADS_WAYLAND_DEBUG");
\treturn enabled;
}

static void
athenaAdsGiantLog(const std::string& line)
{
\tstatic bool initialized = false;
\tstd::ofstream out;
\tif (!initialized)
\t{
\t\tout.open("/tmp/athena-ads-giant-rendering.log",
\t\t         std::ios::out | std::ios::trunc);
\t\tinitialized = true;
\t\tout << "ATHENA_GIANT_LOG start" << std::endl;
\t}
\telse
\t{
\t\tout.open("/tmp/athena-ads-giant-rendering.log",
\t\t         std::ios::out | std::ios::app);
\t}
\tout << line << std::endl;
}

static void
athenaAdsDockLog(const std::string& line, bool reset = false)
{
\tstd::ofstream out;
\tout.open("/tmp/athena-ads-wayland-dock.log",
\t         reset ? std::ios::out : (std::ios::out | std::ios::app));
\tout << line << std::endl;
\tif (athenaAdsWaylandDebugEnabled())
\t{
\t\tstd::cerr << line << std::endl;
\t}
}

static std::string
athenaAdsSizeText(const QSize& size)
{
\treturn std::to_string(size.width()) + "x" + std::to_string(size.height());
}

static std::string
athenaAdsRectText(const QRect& rect)
{
\treturn std::to_string(rect.x()) + "," + std::to_string(rect.y())
\t     + " " + std::to_string(rect.width()) + "x" + std::to_string(rect.height());
}

static const char*
athenaDockAreaName(DockWidgetArea area)
{
\tswitch (area)
\t{
\tcase TopDockWidgetArea: return "Top";
\tcase RightDockWidgetArea: return "Right";
\tcase BottomDockWidgetArea: return "Bottom";
\tcase LeftDockWidgetArea: return "Left";
\tcase CenterDockWidgetArea: return "Center";
\tcase LeftAutoHideArea: return "LeftAutoHide";
\tcase RightAutoHideArea: return "RightAutoHide";
\tcase TopAutoHideArea: return "TopAutoHide";
\tcase BottomAutoHideArea: return "BottomAutoHide";
\tcase InvalidDockWidgetArea: return "Invalid";
\tdefault: return "Other";
\t}
}

static const char*
athenaEventTypeName(QEvent::Type type)
{
\tswitch (type)
\t{
\tcase QEvent::DragEnter: return "DragEnter";
\tcase QEvent::DragMove: return "DragMove";
\tcase QEvent::DragLeave: return "DragLeave";
\tcase QEvent::Drop: return "Drop";
\tdefault: return "Other";
\t}
}

static void
athenaLogWindowSurface(const char* label, QWidget* widget)
{
\tQWindow* window = widget == nullptr ? nullptr : widget->windowHandle();
\tQScreen* screen = window == nullptr ? nullptr : window->screen();
\tstd::ostringstream line;
\tline << "ATHENA_GIANT " << label
\t     << " widget=" << widget
\t     << " widgetClass=" << (widget == nullptr ? "" : widget->metaObject()->className())
\t     << " object=" << (widget == nullptr ? std::string() : widget->objectName().toStdString())
\t     << " title=" << (widget == nullptr ? std::string() : widget->windowTitle().toStdString())
\t     << " isWindow=" << (widget != nullptr && widget->isWindow())
\t     << " visible=" << (widget != nullptr && widget->isVisible())
\t     << " active=" << (widget != nullptr && widget->isActiveWindow())
\t     << " size=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->size())
\t     << " geometry=" << athenaAdsRectText(widget == nullptr ? QRect() : widget->geometry())
\t     << " frame=" << athenaAdsRectText(widget == nullptr ? QRect() : widget->frameGeometry())
\t     << " minimum=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->minimumSize())
\t     << " minimumHint=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->minimumSizeHint())
\t     << " maximum=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->maximumSize())
\t     << " sizeHint=" << athenaAdsSizeText(widget == nullptr ? QSize() : widget->sizeHint())
\t     << " sizePolicy=" << (widget == nullptr ? -1 : int(widget->sizePolicy().horizontalPolicy()))
\t     << "," << (widget == nullptr ? -1 : int(widget->sizePolicy().verticalPolicy()))
\t     << " windowFlags=" << (widget == nullptr ? 0 : quint64(widget->windowFlags()))
\t     << " windowState=" << (widget == nullptr ? 0 : int(widget->windowState()))
\t     << " widgetDpr=" << (widget == nullptr ? 0.0 : widget->devicePixelRatioF())
\t     << " win=" << window
\t     << " winVisible=" << (window != nullptr && window->isVisible())
\t     << " winSize=" << athenaAdsSizeText(window == nullptr ? QSize() : window->size())
\t     << " winGeometry=" << athenaAdsRectText(window == nullptr ? QRect() : window->geometry())
\t     << " winMinimum=" << athenaAdsSizeText(window == nullptr ? QSize() : window->minimumSize())
\t     << " winMaximum=" << athenaAdsSizeText(window == nullptr ? QSize() : window->maximumSize())
\t     << " winFlags=" << (window == nullptr ? 0 : quint64(window->flags()))
\t     << " winState=" << (window == nullptr ? 0 : int(window->windowState()))
\t     << " winDpr=" << (window == nullptr ? 0.0 : window->devicePixelRatio())
\t     << " screenGeometry=" << athenaAdsRectText(screen == nullptr ? QRect() : screen->geometry())
\t     << " screenAvailable=" << athenaAdsRectText(screen == nullptr ? QRect() : screen->availableGeometry());
\tathenaAdsGiantLog(line.str());
\tif (athenaAdsWaylandDebugEnabled())
\t{
\t\tstd::cerr << line.str() << std::endl;
\t}
}

static void
athenaLogTopLevelWindows(const char* label)
{
\tconst auto windows = QGuiApplication::topLevelWindows();
\t{
\t\tstd::ostringstream line;
\t\tline << "ATHENA_GIANT " << label
\t\t     << " topLevelWindowCount=" << windows.size()
\t\t     << " topLevelWidgetCount=" << QApplication::topLevelWidgets().size();
\t\tathenaAdsGiantLog(line.str());
\t\tif (athenaAdsWaylandDebugEnabled())
\t\t{
\t\t\tstd::cerr << line.str() << std::endl;
\t\t}
\t}
\tfor (QWindow* window : windows)
\t{
\t\tstd::ostringstream line;
\t\tline << "ATHENA_GIANT top-level-window"
\t\t     << " label=" << label
\t\t     << " win=" << window
\t\t     << " class=" << (window == nullptr ? "" : window->metaObject()->className())
\t\t     << " title=" << (window == nullptr ? std::string() : window->title().toStdString())
\t\t     << " visible=" << (window != nullptr && window->isVisible())
\t\t     << " size=" << athenaAdsSizeText(window == nullptr ? QSize() : window->size())
\t\t     << " geometry=" << athenaAdsRectText(window == nullptr ? QRect() : window->geometry())
\t\t     << " dpr=" << (window == nullptr ? 0.0 : window->devicePixelRatio());
\t\tathenaAdsGiantLog(line.str());
\t\tif (athenaAdsWaylandDebugEnabled())
\t\t{
\t\t\tstd::cerr << line.str() << std::endl;
\t\t}
\t}
\tfor (QWidget* widget : QApplication::topLevelWidgets())
\t{
\t\tathenaLogWindowSurface("top-level-widget", widget);
\t}
}

static QByteArray
athenaDataStreamPayload(qintptr value)
{
\tQByteArray data;
\tQDataStream stream(&data, QIODevice::WriteOnly);
\tstream << value;
\treturn data;
}

static QByteArray
athenaDataStreamPayload(const QPoint& value)
{
\tQByteArray data;
\tQDataStream stream(&data, QIODevice::WriteOnly);
\tstream << value;
\treturn data;
}

static void
athenaRememberDropTarget(QWidget* widget)
{
\tif (widget == nullptr)
\t{
\t\treturn;
\t}
\tfor (const auto& state : AthenaDropTargetStates)
\t{
\t\tif (state.Widget == widget)
\t\t{
\t\t\treturn;
\t\t}
\t}
\tAthenaDropTargetStates.push_back({widget, widget->acceptDrops()});
\twidget->setAcceptDrops(true);
}

static void
athenaRememberDropTargetTree(QWidget* widget)
{
\tif (widget == nullptr)
\t{
\t\treturn;
\t}
\tathenaRememberDropTarget(widget);
\tconst auto children = widget->findChildren<QWidget*>();
\tfor (QWidget* child : children)
\t{
\t\tathenaRememberDropTarget(child);
\t}
}

static void
athenaPrepareWaylandDockDropTargets(CDockManager* dockManager,
                                    CFloatingDockContainer* activeFloatingContainer)
{
\tAthenaDropTargetStates.clear();
\tif (dockManager != nullptr)
\t{
\t\tfor (CDockContainerWidget* container : dockManager->dockContainers())
\t\t{
\t\t\tif (activeFloatingContainer != nullptr
\t\t\t    && container == activeFloatingContainer->dockContainer())
\t\t\t{
\t\t\t\tcontinue;
\t\t\t}
\t\t\tathenaRememberDropTargetTree(container);
\t\t\tathenaRememberDropTargetTree(container->window());
\t\t}
\t}
}

static void
athenaRestoreWaylandDockDropTargets()
{
\tfor (const auto& state : AthenaDropTargetStates)
\t{
\t\tif (state.Widget)
\t\t{
\t\t\tstate.Widget->setAcceptDrops(state.AcceptDrops);
\t\t}
\t}
\tAthenaDropTargetStates.clear();
}

static const QMimeData*
athenaDockDragMimeData(QEvent* event)
{
\tswitch (event->type())
\t{
\tcase QEvent::DragEnter:
\t\treturn static_cast<QDragEnterEvent*>(event)->mimeData();
\tcase QEvent::DragMove:
\t\treturn static_cast<QDragMoveEvent*>(event)->mimeData();
\tcase QEvent::Drop:
\t\treturn static_cast<QDropEvent*>(event)->mimeData();
\tdefault:
\t\treturn nullptr;
\t}
}

static QPoint
athenaDockDragEventPosition(QEvent* event)
{
\tswitch (event->type())
\t{
\tcase QEvent::DragEnter:
\t\treturn static_cast<QDragEnterEvent*>(event)->position().toPoint();
\tcase QEvent::DragMove:
\t\treturn static_cast<QDragMoveEvent*>(event)->position().toPoint();
\tcase QEvent::Drop:
\t\treturn static_cast<QDropEvent*>(event)->position().toPoint();
\tdefault:
\t\treturn QCursor::pos();
\t}
}

static QPoint
athenaDockDragGlobalPosition(QObject* target, QEvent* event)
{
\tconst QPoint localPos = athenaDockDragEventPosition(event);
\tconst QPoint cursorPos = QCursor::pos();
\tQPoint globalPos = cursorPos;
\tconst char* source = "cursor-fallback";

\tif (QWidget* widget = qobject_cast<QWidget*>(target))
\t{
\t\tglobalPos = widget->mapToGlobal(localPos);
\t\tsource = "widget";
\t\tstd::ostringstream line;
\t\tline << "ATHENA_DOCK_POS"
\t\t     << " event=" << athenaEventTypeName(event->type())
\t\t     << " source=" << source
\t\t     << " target=" << target
\t\t     << " class=" << widget->metaObject()->className()
\t\t     << " object=" << widget->objectName().toStdString()
\t\t     << " local=" << localPos.x() << "," << localPos.y()
\t\t     << " global=" << globalPos.x() << "," << globalPos.y()
\t\t     << " cursor=" << cursorPos.x() << "," << cursorPos.y()
\t\t     << " widgetGeom=" << athenaAdsRectText(widget->geometry())
\t\t     << " widgetFrame=" << athenaAdsRectText(widget->frameGeometry())
\t\t     << " windowGeom=" << athenaAdsRectText(widget->window()->geometry())
\t\t     << " windowFrame=" << athenaAdsRectText(widget->window()->frameGeometry());
\t\tathenaAdsDockLog(line.str());
\t\treturn globalPos;
\t}

\tif (QWindow* window = qobject_cast<QWindow*>(target))
\t{
\t\tglobalPos = window->mapToGlobal(localPos);
\t\tsource = "window";
\t\tstd::ostringstream line;
\t\tline << "ATHENA_DOCK_POS"
\t\t     << " event=" << athenaEventTypeName(event->type())
\t\t     << " source=" << source
\t\t     << " target=" << target
\t\t     << " class=" << window->metaObject()->className()
\t\t     << " title=" << window->title().toStdString()
\t\t     << " local=" << localPos.x() << "," << localPos.y()
\t\t     << " global=" << globalPos.x() << "," << globalPos.y()
\t\t     << " cursor=" << cursorPos.x() << "," << cursorPos.y()
\t\t     << " winGeom=" << athenaAdsRectText(window->geometry())
\t\t     << " winSize=" << athenaAdsSizeText(window->size())
\t\t     << " dpr=" << window->devicePixelRatio();
\t\tathenaAdsDockLog(line.str());
\t\treturn globalPos;
\t}

\tstd::ostringstream line;
\tline << "ATHENA_DOCK_POS"
\t     << " event=" << athenaEventTypeName(event->type())
\t     << " source=" << source
\t     << " target=" << target
\t     << " targetClass=" << (target == nullptr ? "" : target->metaObject()->className())
\t     << " local=" << localPos.x() << "," << localPos.y()
\t     << " global=" << globalPos.x() << "," << globalPos.y()
\t     << " cursor=" << cursorPos.x() << "," << cursorPos.y();
\tathenaAdsDockLog(line.str());
\treturn globalPos;
}

class AthenaAdsWaylandDockDragFilter : public QObject
{
public:
\tbool eventFilter(QObject* target, QEvent* event) override
\t{
\t\tif (!AthenaActiveWaylandDockDrag)
\t\t{
\t\t\treturn QObject::eventFilter(target, event);
\t\t}

\t\tif (athenaAdsWaylandDebugEnabled())
\t\t{
\t\t\tconst QEvent::Type type = event->type();
\t\t\tif (type == QEvent::Show || type == QEvent::Hide || type == QEvent::Resize
\t\t\t    || type == QEvent::Move || type == QEvent::Expose || type == QEvent::Paint)
\t\t\t{
\t\t\t\tif (auto* window = qobject_cast<QWindow*>(target))
\t\t\t\t{
\t\t\t\t\tqDebug() << "ATHENA_ADS_WAYLAND event-window"
\t\t\t\t\t         << "type" << int(type)
\t\t\t\t\t         << "target" << window
\t\t\t\t\t         << "class" << window->metaObject()->className()
\t\t\t\t\t         << "title" << window->title()
\t\t\t\t\t         << "visible" << window->isVisible()
\t\t\t\t\t         << "size" << window->size()
\t\t\t\t\t         << "geometry" << window->geometry()
\t\t\t\t\t         << "dpr" << window->devicePixelRatio();
\t\t\t\t}
\t\t\t\telse if (auto* widget = qobject_cast<QWidget*>(target))
\t\t\t\t{
\t\t\t\t\tif (widget->isWindow() || widget->window() == AthenaActiveWaylandDockDrag)
\t\t\t\t\t{
\t\t\t\t\t\tqDebug() << "ATHENA_ADS_WAYLAND event-widget"
\t\t\t\t\t\t         << "type" << int(type)
\t\t\t\t\t\t         << "target" << widget
\t\t\t\t\t\t         << "class" << widget->metaObject()->className()
\t\t\t\t\t\t         << "object" << widget->objectName()
\t\t\t\t\t\t         << "title" << widget->windowTitle()
\t\t\t\t\t\t         << "visible" << widget->isVisible()
\t\t\t\t\t\t         << "size" << widget->size()
\t\t\t\t\t\t         << "geometry" << widget->geometry()
\t\t\t\t\t\t         << "frame" << widget->frameGeometry();
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}
\t\t}

\t\tif (event->type() == QEvent::DragLeave)
\t\t{
\t\t\tAthenaActiveWaylandDockDrag->athenaHideWaylandDockOverlays();
\t\t\treturn QObject::eventFilter(target, event);
\t\t}

\t\tconst QMimeData* mimeData = athenaDockDragMimeData(event);
\t\tif (mimeData == nullptr || !mimeData->hasFormat(AthenaAdsFloatingDockMime))
\t\t{
\t\t\treturn QObject::eventFilter(target, event);
\t\t}

\t\tconst QPoint globalPos = athenaDockDragGlobalPosition(target, event);
\t\t{
\t\t\tstd::ostringstream line;
\t\t\tline << "ATHENA_DOCK_EVENT"
\t\t\t     << " event=" << athenaEventTypeName(event->type())
\t\t\t     << " target=" << target
\t\t\t     << " targetClass=" << (target == nullptr ? "" : target->metaObject()->className())
\t\t\t     << " global=" << globalPos.x() << "," << globalPos.y()
\t\t\t     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y();
\t\t\tathenaAdsDockLog(line.str());
\t\t}
\t\tAthenaActiveWaylandDockDrag->athenaUpdateWaylandDockDrag(globalPos);

\t\tswitch (event->type())
\t\t{
\t\tcase QEvent::DragEnter:
\t\t{
\t\t\tauto* dragEvent = static_cast<QDragEnterEvent*>(event);
\t\t\tdragEvent->setDropAction(Qt::MoveAction);
\t\t\tdragEvent->accept();
\t\t\treturn true;
\t\t}
\t\tcase QEvent::DragMove:
\t\t{
\t\t\tauto* dragEvent = static_cast<QDragMoveEvent*>(event);
\t\t\tdragEvent->setDropAction(Qt::MoveAction);
\t\t\tdragEvent->accept();
\t\t\treturn true;
\t\t}
\t\tcase QEvent::Drop:
\t\t{
\t\t\tauto* dropEvent = static_cast<QDropEvent*>(event);
\t\t\tconst bool docked = AthenaActiveWaylandDockDrag->athenaFinishWaylandDockDrag(true);
\t\t\tif (docked)
\t\t\t{
\t\t\t\tdropEvent->setDropAction(Qt::MoveAction);
\t\t\t\tdropEvent->accept();
\t\t\t}
\t\t\telse
\t\t\t{
\t\t\t\tdropEvent->ignore();
\t\t\t}
\t\t\treturn true;
\t\t}
\t\tdefault:
\t\t\tbreak;
\t\t}
\t\treturn QObject::eventFilter(target, event);
\t}
};

static AthenaAdsWaylandDockDragFilter*
athenaWaylandDockDragFilter()
{
\tstatic auto* filter = new AthenaAdsWaylandDockDragFilter();
\treturn filter;
}
#endif
'''


WAYLAND_DOCK_DRAG_METHODS = '''bool CFloatingDockContainer::athenaWaylandDockDragActive() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\treturn AthenaActiveWaylandDockDrag == this;
#else
\treturn false;
#endif
}

bool CFloatingDockContainer::athenaWaylandDockDragStarted() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\treturn d->AthenaWaylandDockDragStarted;
#else
\treturn false;
#endif
}

bool CFloatingDockContainer::athenaTryStartWaylandDockDrag(const QPoint& hotSpot, QWidget* sourceWidget)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\td->AthenaWaylandDockDragStarted = false;
\tif (!athenaIsNativeWaylandPlatform())
\t{
\t\treturn false;
\t}

\t{
\t\tstd::ostringstream line;
\t\tline << "ATHENA_DOCK_START"
\t\t     << " floating=" << this
\t\t     << " sourceWidget=" << sourceWidget
\t\t     << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
\t\t     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y();
\t\tathenaAdsDockLog(line.str(), true);
\t}

\tathenaLogWindowSurface("try-start-entry", this);
\tathenaLogWindowSurface("try-start-source-widget", sourceWidget);
\tathenaLogTopLevelWindows("before-floating-show");

\tif (!isVisible())
\t{
\t\tshow();
\t}
\tathenaLogWindowSurface("after-floating-show", this);
\tQWindow* floatingWindow = windowHandle();
\tif (floatingWindow == nullptr)
\t{
\t\treturn false;
\t}

\t// Match Qt's own QMainWindow platform drag contract: the QDrag source is
\t// the detached top-level that is named in the private MIME payload.  Using
\t// the main window as the source while asking QtWayland to move this floating
\t// toplevel puts the main window into the drag's Qt-side surface path.
\tQWidget* dragSource = this;
\tathenaLogWindowSurface("drag-source", dragSource);
\tif (dragSource == nullptr)
\t{
\t\treturn false;
\t}

\tQDrag drag(dragSource);
\t{
\t\tstd::ostringstream line;
\t\tline << "ATHENA_GIANT qdrag-created"
\t\t     << " dragSource=" << dragSource
\t\t     << " dragSourceWindow=" << dragSource->window()
\t\t     << " floating=" << this
\t\t     << " floatingWindow=" << floatingWindow
\t\t     << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
\t\t     << " dragPixmapNull=" << drag.pixmap().isNull()
\t\t     << " dragPixmapSize=" << athenaAdsSizeText(drag.pixmap().size())
\t\t     << " dragPixmapDpr=" << drag.pixmap().devicePixelRatio()
\t\t     << " floatingWindowSize=" << athenaAdsSizeText(floatingWindow->size())
\t\t     << " floatingWindowGeometry=" << athenaAdsRectText(floatingWindow->geometry())
\t\t     << " floatingWindowDpr=" << floatingWindow->devicePixelRatio();
\t\tathenaAdsGiantLog(line.str());
\t}
\tif (athenaAdsWaylandDebugEnabled())
\t{
\t\tstd::cerr << "ATHENA_ADS_WAYLAND qdrag-created"
\t\t          << " dragSource=" << dragSource
\t\t          << " dragSourceWindow=" << dragSource->window()
\t\t          << " floating=" << this
\t\t          << " floatingWindow=" << floatingWindow
\t\t          << " hotSpot=" << hotSpot.x() << "," << hotSpot.y()
\t\t          << " dragPixmapNull=" << drag.pixmap().isNull()
\t\t          << " dragPixmapSize=" << athenaAdsSizeText(drag.pixmap().size())
\t\t          << " dragPixmapDpr=" << drag.pixmap().devicePixelRatio()
\t\t          << " floatingWindowSize=" << athenaAdsSizeText(floatingWindow->size())
\t\t          << " floatingWindowGeometry=" << athenaAdsRectText(floatingWindow->geometry())
\t\t          << " floatingWindowDpr=" << floatingWindow->devicePixelRatio()
\t\t          << std::endl;
\t}
\tauto* mimeData = new QMimeData();
\tmimeData->setData(AthenaAdsFloatingDockMime, QByteArrayLiteral("1"));
\tmimeData->setData(AthenaQtMainWindowDragWindowMime,
\t                  athenaDataStreamPayload(reinterpret_cast<qintptr>(floatingWindow)));
\tmimeData->setData(AthenaQtMainWindowDragPositionMime,
\t                  athenaDataStreamPayload(hotSpot));
\tdrag.setMimeData(mimeData);

\tAthenaActiveWaylandDockDrag = this;
\td->AthenaWaylandDockDragStarted = true;
\td->AthenaWaylandHasLastGlobalPos = false;
\tathenaClearWaylandDockOverlayCursorPosition();
\tathenaPrepareWaylandDockDropTargets(d->DockManager, this);
\tqApp->installEventFilter(athenaWaylandDockDragFilter());
\tathenaLogTopLevelWindows("before-qdrag-exec");
\tdrag.exec(Qt::MoveAction, Qt::MoveAction);
\tathenaLogTopLevelWindows("after-qdrag-exec");
\tathenaLogWindowSurface("after-qdrag-floating", this);
\tQPointer<CFloatingDockContainer> delayedFloating(this);
\tQTimer::singleShot(0, this, [delayedFloating]() {
\t\tif (delayedFloating)
\t\t{
\t\t\tathenaLogWindowSurface("after-qdrag-floating-0ms", delayedFloating);
\t\t}
\t});
\tQTimer::singleShot(100, this, [delayedFloating]() {
\t\tif (delayedFloating)
\t\t{
\t\t\tathenaLogWindowSurface("after-qdrag-floating-100ms", delayedFloating);
\t\t}
\t});
\tQTimer::singleShot(500, this, [delayedFloating]() {
\t\tif (delayedFloating)
\t\t{
\t\t\tathenaLogWindowSurface("after-qdrag-floating-500ms", delayedFloating);
\t\t}
\t});
\tif (athenaWaylandDockDragActive())
\t{
\t\tathenaFinishWaylandDockDrag(false);
\t}
\tqApp->removeEventFilter(athenaWaylandDockDragFilter());
\tathenaRestoreWaylandDockDropTargets();
\tathenaClearWaylandDockOverlayCursorPosition();
\treturn true;
#else
\tQ_UNUSED(hotSpot)
\tQ_UNUSED(sourceWidget)
\treturn false;
#endif
}

void CFloatingDockContainer::athenaUpdateWaylandDockDrag(const QPoint& globalPos)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tif (athenaWaylandDockDragActive())
\t{
\t\td->AthenaWaylandLastGlobalPos = globalPos;
\t\td->AthenaWaylandHasLastGlobalPos = true;
\t\tathenaSetWaylandDockOverlayCursorPosition(globalPos);
\t\td->updateDropOverlays(globalPos);
\t\tif (d->DockManager)
\t\t{
\t\t\tDockWidgetArea containerArea =
\t\t\t\td->DockManager->containerOverlay()->dropAreaUnderCursor();
\t\t\tDockWidgetArea dockArea =
\t\t\t\td->DockManager->dockAreaOverlay()->dropAreaUnderCursor();
\t\t\tQWidget* dropWidget = d->DropContainer;
\t\t\tQPoint localPos = dropWidget == nullptr
\t\t\t\t? QPoint()
\t\t\t\t: dropWidget->mapFromGlobal(globalPos);
\t\t\tstd::ostringstream line;
\t\t\tline << "ATHENA_DOCK_OVERLAY"
\t\t\t     << " global=" << globalPos.x() << "," << globalPos.y()
\t\t\t     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y()
\t\t\t     << " dropContainer=" << dropWidget
\t\t\t     << " dropClass=" << (dropWidget == nullptr ? "" : dropWidget->metaObject()->className())
\t\t\t     << " dropObject=" << (dropWidget == nullptr ? std::string() : dropWidget->objectName().toStdString())
\t\t\t     << " dropIsFloating=" << (d->DropContainer && d->DropContainer->isFloating())
\t\t\t     << " dropGeom=" << athenaAdsRectText(dropWidget == nullptr ? QRect() : dropWidget->geometry())
\t\t\t     << " dropFrame=" << athenaAdsRectText(dropWidget == nullptr ? QRect() : dropWidget->frameGeometry())
\t\t\t     << " local=" << localPos.x() << "," << localPos.y()
\t\t\t     << " containerArea=" << athenaDockAreaName(containerArea)
\t\t\t     << "(" << int(containerArea) << ")"
\t\t\t     << " dockArea=" << athenaDockAreaName(dockArea)
\t\t\t     << "(" << int(dockArea) << ")";
\t\t\tathenaAdsDockLog(line.str());
\t\t}
\t}
#else
\tQ_UNUSED(globalPos)
#endif
}

bool CFloatingDockContainer::athenaHasWaylandDockTarget() const
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tif (!d->DockManager || d->DropContainer == nullptr)
\t{
\t\treturn false;
\t}
\treturn d->DockManager->dockAreaOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea
\t    || d->DockManager->containerOverlay()->dropAreaUnderCursor() != InvalidDockWidgetArea;
#else
\treturn false;
#endif
}

bool CFloatingDockContainer::athenaFinishWaylandDockDrag(bool dropped)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tif (!athenaWaylandDockDragActive())
\t{
\t\treturn false;
\t}

\tconst QPoint dropPos = d->AthenaWaylandHasLastGlobalPos
\t\t? d->AthenaWaylandLastGlobalPos
\t\t: QCursor::pos();
\tathenaSetWaylandDockOverlayCursorPosition(dropPos);
\tconst bool shouldDock = dropped && athenaHasWaylandDockTarget();
\tQPointer<CDockManager> dockManager = d->DockManager;
\tQPointer<CDockContainerWidget> dropContainer = d->DropContainer;
\t{
\t\tDockWidgetArea containerArea = dockManager
\t\t\t? dockManager->containerOverlay()->dropAreaUnderCursor()
\t\t\t: InvalidDockWidgetArea;
\t\tDockWidgetArea dockArea = dockManager
\t\t\t? dockManager->dockAreaOverlay()->dropAreaUnderCursor()
\t\t\t: InvalidDockWidgetArea;
\t\tQPoint localPos = dropContainer
\t\t\t? dropContainer->mapFromGlobal(dropPos)
\t\t\t: QPoint();
\t\tstd::ostringstream line;
\t\tline << "ATHENA_DOCK_FINISH"
\t\t     << " dropped=" << dropped
\t\t     << " shouldDock=" << shouldDock
\t\t     << " dropPos=" << dropPos.x() << "," << dropPos.y()
\t\t     << " cursor=" << QCursor::pos().x() << "," << QCursor::pos().y()
\t\t     << " dropContainer=" << dropContainer.data()
\t\t     << " dropIsFloating=" << (dropContainer && dropContainer->isFloating())
\t\t     << " local=" << localPos.x() << "," << localPos.y()
\t\t     << " containerArea=" << athenaDockAreaName(containerArea)
\t\t     << "(" << int(containerArea) << ")"
\t\t     << " dockArea=" << athenaDockAreaName(dockArea)
\t\t     << "(" << int(dockArea) << ")";
\t\tathenaAdsDockLog(line.str());
\t}
\td->setState(DraggingInactive);
\tif (AthenaActiveWaylandDockDrag == this)
\t{
\t\tAthenaActiveWaylandDockDrag.clear();
\t}
\tif (shouldDock)
\t{
\t\tif (!dropContainer)
\t\t{
\t\t\tif (dockManager)
\t\t\t{
\t\t\t\tdockManager->containerOverlay()->hideOverlay();
\t\t\t\tdockManager->dockAreaOverlay()->hideOverlay();
\t\t\t}
\t\t\td->DropContainer = nullptr;
\t\t\td->AthenaWaylandHasLastGlobalPos = false;
\t\t\tathenaClearWaylandDockOverlayCursorPosition();
\t\t\treturn false;
\t\t}
\t\tdropContainer->dropFloatingWidget(this, dropPos);
\t}
\telse
\t{
\t\tathenaHideWaylandDockOverlays();
\t}
\tif (dockManager)
\t{
\t\tdockManager->containerOverlay()->hideOverlay();
\t\tdockManager->dockAreaOverlay()->hideOverlay();
\t}
\td->DropContainer = nullptr;
\td->AthenaWaylandHasLastGlobalPos = false;
\tathenaClearWaylandDockOverlayCursorPosition();
\treturn shouldDock;
#else
\tQ_UNUSED(dropped)
\treturn false;
#endif
}

void CFloatingDockContainer::athenaHideWaylandDockOverlays()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
\tif (d->DockManager)
\t{
\t\td->DockManager->containerOverlay()->hideOverlay();
\t\td->DockManager->dockAreaOverlay()->hideOverlay();
\t}
\td->DropContainer = nullptr;
\td->AthenaWaylandHasLastGlobalPos = false;
\tathenaClearWaylandDockOverlayCursorPosition();
#endif
}

'''


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
        'find_package(Qt6 COMPONENTS Core Gui Widgets GuiPrivate REQUIRED)\n')
    content = content.replace(
        'target_link_libraries(${library_name} PUBLIC Qt${QT_VERSION_MAJOR}::Core \n'
        '                                               Qt${QT_VERSION_MAJOR}::Gui \n'
        '                                               Qt${QT_VERSION_MAJOR}::Widgets)\n',
        'target_link_libraries(${library_name} PUBLIC Qt6::Core Qt6::Gui '
        'Qt6::Widgets PRIVATE Qt6::GuiPrivate)\n')

    write_if_changed(path, content, original_content)


def patch_ads_wayland_focus_hiding(base_dir):
    path = os.path.join(base_dir, 'src/DockManager.cpp')
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return

    with open(path, 'r') as f:
        content = f.read()

    original_content = content
    content = content.replace(
        '''            if(QGuiApplication::platformName() == QLatin1String("xcb"))
\t\t\t{
\t\t\t\tinternal::xcb_update_prop(true, _window->window()->winId(),
                    "_NET_WM_STATE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_STAYS_ON_TOP");
\t\t\t}
\t\t\telse
\t\t\t{
                    _window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
\t\t\t}
''',
        '''            if(QGuiApplication::platformName() == QLatin1String("xcb"))
\t\t\t{
\t\t\t\tinternal::xcb_update_prop(true, _window->window()->winId(),
                    "_NET_WM_STATE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_STAYS_ON_TOP");
\t\t\t}
''',
        1)
    content = content.replace(
        '''            if(QGuiApplication::platformName() == QLatin1String("xcb"))
\t\t\t{
\t\t\t\tinternal::xcb_update_prop(false, _window->window()->winId(),
                    "_NET_WM_STATE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_STAYS_ON_TOP");
\t\t\t}
            else
\t\t\t{
\t\t\t\t_window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
\t\t\t}
\t\t\t_window->raise();
''',
        '''            if(QGuiApplication::platformName() == QLatin1String("xcb"))
\t\t\t{
\t\t\t\tinternal::xcb_update_prop(false, _window->window()->winId(),
                    "_NET_WM_STATE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_STAYS_ON_TOP");
\t\t\t\t_window->raise();
\t\t\t}
''',
        1)
    content = content.replace(
        '''\t// Window always on top of the MainWindow.
\tif (e->type() == QEvent::WindowActivate)
''',
        '''\t// Window always on top of the MainWindow. Keep this emulation XCB-only:
\t// changing Qt window flags on native Wayland remaps/hides floating panes
\t// when focus moves between the main window and a floated dock container.
\tif (e->type() == QEvent::WindowActivate)
''',
        1)

    write_if_changed(path, content, original_content)


# If run from CMake, the first argument is the ADS source directory.
base_dir = sys.argv[1] if len(sys.argv) > 1 else "."

patch_ads_floating_windows(base_dir)
patch_ads_initial_wayland_drag(base_dir)
patch_ads_wayland_single_floating_tabbar(base_dir)
patch_ads_wayland_overlay_position(base_dir)
patch_ads_wayland_focus_hiding(base_dir)
patch_ads_qt6_private_gui(base_dir)
