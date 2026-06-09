
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

# If run from CMake, the first argument is the source directory
base_dir = sys.argv[1] if len(sys.argv) > 1 else "."

# Patch C++ files
cpp_files = [
    'src/DockAreaTitleBar.cpp',
    'src/DockManager.cpp',
    'src/DockWidget.cpp'
]

for cpp in cpp_files:
    path = os.path.join(base_dir, cpp)
    patch_file(path, ['QSize(16, 16)', 'QSize(24, 24)'], 'QSize(32, 32)')

# Patch Stylesheets
css_files = [
    'src/stylesheets/default.css',
    'src/stylesheets/default_linux.css',
    'src/stylesheets/default_windows.css'
]

for css in css_files:
    path = os.path.join(base_dir, css)
    patch_file(path, ['qproperty-iconSize: 16px;', 'qproperty-iconSize: 24px;'], 'qproperty-iconSize: 32px;')
    patch_file(path, ['qproperty-iconSize: 16px 16px;', 'qproperty-iconSize: 24px 24px;'], 'qproperty-iconSize: 32px 32px;')

patch_ads_floating_windows(base_dir)
patch_ads_qt6_private_gui(base_dir)
