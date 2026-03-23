
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
