# Native UIKit integration. Qt retains ownership of UIApplicationMain and QPA.
enable_language (OBJCXX)
set (ATHENA_IOS_SOURCES
  "${ATHENA_SOURCE_DIR}/src/Subsystems/iOS/ios_paths.mm"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/iOS/ios_application.mm"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/iOS/ios_tls_trust.mm")
set_source_files_properties (${ATHENA_IOS_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-fobjc-arc")
target_sources (athena_body PRIVATE ${ATHENA_IOS_SOURCES})
set_property (TARGET athena_body PROPERTY OBJCXX_STANDARD 17)
set_property (TARGET athena_body PROPERTY OBJCXX_STANDARD_REQUIRED ON)
configure_file ("${ATHENA_SOURCE_DIR}/src/Subsystems/iOS/Info.plist.in"
  "${ATHENA_BINARY_DIR}/ATHENA-Info.plist.in" @ONLY)
set_target_properties (${ATHENA_binary_name} PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${ATHENA_BINARY_DIR}/ATHENA-Info.plist.in"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.athena.editor"
  XCODE_ATTRIBUTE_PRODUCT_NAME "ATHENA"
  XCODE_ATTRIBUTE_EXECUTABLE_NAME "ATHENA.bin")

function (athena_ios_resources root destination)
  file (GLOB_RECURSE resources LIST_DIRECTORIES false "${root}/*")
  foreach (resource IN LISTS resources)
    file (RELATIVE_PATH relative "${root}" "${resource}")
    get_filename_component (directory "${relative}" DIRECTORY)
    set_source_files_properties ("${resource}" PROPERTIES
      HEADER_FILE_ONLY TRUE
      MACOSX_PACKAGE_LOCATION "Resources/${destination}/${directory}")
  endforeach ()
  target_sources (${ATHENA_binary_name} PRIVATE ${resources})
endfunction ()

foreach (directory progs styles packages langs misc doc fonts)
  athena_ios_resources ("${ATHENA_SOURCE_DIR}/ATHENA/${directory}" "ATHENA/${directory}")
endforeach ()
athena_ios_resources ("${ATHENA_GUILE_PREFIX}/share/guile/3.0"
  "ATHENA/lib/athena-guile/share/guile/3.0")
athena_ios_resources ("${ATHENA_GUILE_PREFIX}/lib/guile/3.0/ccache"
  "ATHENA/lib/athena-guile/lib/guile/3.0/ccache")
# ATHENA's Info.plist uses the modern UILaunchScreen declaration.  Do not let
# Qt add its default storyboard as well: with a deployment target newer than
# the installed SDK, ibtool rejects that redundant storyboard even though
# clang and the linker can encode the intended minimum OS version.
set (QT_NO_SET_DEFAULT_IOS_LAUNCH_SCREEN ON)
qt_finalize_executable (${ATHENA_binary_name})
