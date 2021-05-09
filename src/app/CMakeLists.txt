# Generate and configure translation files
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# Based on https://gist.github.com/giraldeau/546ba5512a74dfe9d8ea0862d66db412
file(GLOB QBT_TS_FILES_LIST "${qBittorrent_SOURCE_DIR}/src/lang/*.ts")
set_source_files_properties(${QBT_TS_FILES_LIST} PROPERTIES OUTPUT_LOCATION "${qBittorrent_BINARY_DIR}/src/lang")
qt6_add_translation(QBT_QM_FILES_LIST ${QBT_TS_FILES_LIST} OPTIONS -silent)
configure_file("${qBittorrent_SOURCE_DIR}/src/lang/lang.qrc" "${qBittorrent_BINARY_DIR}/src/lang/lang.qrc" COPYONLY)

if (WEBUI)
    file(GLOB QBT_WEBUI_TS_FILES_LIST "${qBittorrent_SOURCE_DIR}/src/webui/www/translations/*.ts")
    set_source_files_properties(${QBT_WEBUI_TS_FILES_LIST}
        PROPERTIES OUTPUT_LOCATION "${qBittorrent_BINARY_DIR}/src/webui/www/translations")
    qt6_add_translation(QBT_WEBUI_QM_FILES_LIST ${QBT_WEBUI_TS_FILES_LIST} OPTIONS -silent)
    configure_file("${qBittorrent_SOURCE_DIR}/src/webui/www/translations/webui_translations.qrc"
        "${qBittorrent_BINARY_DIR}/src/webui/www/translations/webui_translations.qrc" COPYONLY)
endif()

FILE(GLOB QT_TRANSLATIONS_LIST "${qBittorrent_SOURCE_DIR}/dist/qt-translations/qtbase_*.qm")
foreach(EXTRA_TRANSLATION IN ITEMS "fa" "gl" "lt" "pt" "sl" "sv" "zh_CN")
    list(APPEND QT_TRANSLATIONS_LIST "${qBittorrent_SOURCE_DIR}/dist/qt-translations/qt_${EXTRA_TRANSLATION}.qm")
endforeach()

# Executable target configuration
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
add_executable(qbt_app)

target_sources(qbt_app PRIVATE
    # headers
    application.h
    applicationinstancemanager.h
    cmdoptions.h
    filelogger.h
    qtlocalpeer/qtlocalpeer.h
    upgrade.h

    # sources
    application.cpp
    applicationinstancemanager.cpp
    cmdoptions.cpp
    filelogger.cpp
    main.cpp
    qtlocalpeer/qtlocalpeer.cpp
    upgrade.cpp

    # resources
    "${qBittorrent_SOURCE_DIR}/src/icons/icons.qrc"
    "${qBittorrent_SOURCE_DIR}/src/searchengine/searchengine.qrc"
    ${QBT_QM_FILES_LIST}
    "${qBittorrent_BINARY_DIR}/src/lang/lang.qrc" # yes, it's supposed to be "*_BINARY_DIR"
)

target_link_libraries(qbt_app PRIVATE
    qbt_base
)

set_target_properties(qbt_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (GUI)
    set_target_properties(qbt_app PROPERTIES OUTPUT_NAME qbittorrent)
else()
    set_target_properties(qbt_app PROPERTIES OUTPUT_NAME qbittorrent-nox)
endif()

# Additional platform specific configuration
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
set_source_files_properties(${QT_TRANSLATIONS_LIST} PROPERTIES MACOSX_PACKAGE_LOCATION translations)
set_source_files_properties(
    "${qBittorrent_SOURCE_DIR}/dist/mac/qt.conf"
    "${qBittorrent_SOURCE_DIR}/dist/mac/qBitTorrentDocument.icns"
    "${qBittorrent_SOURCE_DIR}/dist/mac/qbittorrent_mac.icns"
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # provide variables for substitution in dist/mac/Info.plist
    get_target_property(EXECUTABLE_NAME qbt_app OUTPUT_NAME)
    # This variable name should be changed once qmake is no longer used. Refer to the discussion in PR #14813
    set(MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    set_target_properties(qbt_app PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "qBittorrent"
        MACOSX_BUNDLE_INFO_PLIST "${qBittorrent_SOURCE_DIR}/dist/mac/Info.plist"
    )
    target_sources(qbt_app PRIVATE
        ${QT_TRANSLATIONS_LIST}
        "${qBittorrent_SOURCE_DIR}/dist/mac/qt.conf"
        "${qBittorrent_SOURCE_DIR}/dist/mac/qBitTorrentDocument.icns"
        "${qBittorrent_SOURCE_DIR}/dist/mac/qbittorrent_mac.icns"
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(qbt_app PROPERTIES WIN32_EXECUTABLE ON)
    if (MINGW)
        target_sources(qbt_app PRIVATE "${qBittorrent_SOURCE_DIR}/src/qbittorrent_mingw.rc")
    else()
        target_sources(qbt_app PRIVATE "${qBittorrent_SOURCE_DIR}/src/qbittorrent.rc")
    endif()
    target_sources(qbt_app PRIVATE "${qBittorrent_SOURCE_DIR}/src/qbittorrent.exe.manifest")

    # Copy DLLs to the build folder after the build, so that the executable can be run from there.
    # When using vcpkg, everything is automatically copied except for some stuff handled by windeployqt;
    # this is no problem because the copy_if_different command is idempotent in this case
    # TODO: works for MSVC, what about MinGW?
    # TODO: This can probably be greatly simplified by using TARGET_RUNTIME_DLLS instead, in CMake >= 3.21
    if (MSVC_RUNTIME_DYNAMIC)
        # Must manually find these DLLs, because the target names for these libs point at the .lib files, not the .dll files
        # Handling zlib is trickier than OpenSSL because FindZLIB sucks
        cmake_path(SET OPENSSL_DLLS_PATH "${OPENSSL_CRYPTO_LIBRARY}")
        cmake_path(GET OPENSSL_DLLS_PATH PARENT_PATH OPENSSL_DLLS_PATH)
        cmake_path(SET OPENSSL_DLLS_PATH NORMALIZE "${OPENSSL_DLLS_PATH}/../bin")
        find_file(OPENSSL_LIBSSL_DLL "libssl-1_1-x64.dll" PATHS "${OPENSSL_DLLS_PATH}" DOC "OpenSSL::SSL DLL path" REQUIRED
            NO_DEFAULT_PATH NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH
        )
        find_file(OPENSSL_LIBCRYPTO_DLL "libcrypto-1_1-x64.dll" PATHS "${OPENSSL_DLLS_PATH}" DOC "OpenSSL::Crypto DLL path" REQUIRED
            NO_DEFAULT_PATH NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH
        )
        list(APPEND ZLIB_NAMES_LIST "z.dll" "zlib.dll" "zdll.dll" "zlib1.dll" "zd.dll" "zlibd.dll" "zdlld.dll" "zlibd1.dll" "zlib1d.dll")
        get_target_property(ZLIB_RELEASE_LOCATION ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
        get_target_property(ZLIB_DEBUG_LOCATION ZLIB::ZLIB IMPORTED_LOCATION_DEBUG)
        get_target_property(ZLIB_ANY_LOCATION ZLIB::ZLIB IMPORTED_LOCATION)
        if (ZLIB_RELEASE_LOCATION)
            cmake_path(SET ZLIB_RELEASE_LOCATION "${ZLIB_RELEASE_LOCATION}")
            cmake_path(GET ZLIB_RELEASE_LOCATION PARENT_PATH ZLIB_RELEASE_LOCATION)
            cmake_path(SET ZLIB_RELEASE_LOCATION NORMALIZE "${ZLIB_RELEASE_LOCATION}/../bin")
            find_file(ZLIB_DLL_REL ${ZLIB_NAMES_LIST} PATHS "${ZLIB_RELEASE_LOCATION}" DOC "ZLIB::ZLIB DLL path (Release)" REQUIRED
                NO_DEFAULT_PATH NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        endif()
        if (ZLIB_DEBUG_LOCATION)
            cmake_path(SET ZLIB_DEBUG_LOCATION "${ZLIB_DEBUG_LOCATION}")
            cmake_path(GET ZLIB_DEBUG_LOCATION PARENT_PATH ZLIB_DEBUG_LOCATION)
            cmake_path(SET ZLIB_DEBUG_LOCATION NORMALIZE "${ZLIB_DEBUG_LOCATION}/../bin")
            find_file(ZLIB_DLL_DBG ${ZLIB_NAMES_LIST} PATHS "${ZLIB_DEBUG_LOCATION}" DOC "ZLIB::ZLIB DLL path (Debug)" REQUIRED
                NO_DEFAULT_PATH NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        endif()
        if (ZLIB_ANY_LOCATION)
            cmake_path(SET ZLIB_ANY_LOCATION "${ZLIB_ANY_LOCATION}")
            cmake_path(GET ZLIB_ANY_LOCATION PARENT_PATH ZLIB_ANY_LOCATION)
            cmake_path(SET ZLIB_ANY_LOCATION NORMALIZE "${ZLIB_ANY_LOCATION}/../bin")
            find_file(ZLIB_DLL_ANY ${ZLIB_NAMES_LIST} PATHS "${ZLIB_ANY_LOCATION}" DOC "ZLIB::ZLIB DLL path (any)" REQUIRED
                NO_DEFAULT_PATH NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH
            )
            set(ZLIB_DLL_REL "${ZLIB_DLL_ANY}")
            set(ZLIB_DLL_DBG "${ZLIB_DLL_ANY}")
        endif()

        add_custom_command(TARGET qbt_app POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${ZLIB_DLL_DBG},${ZLIB_DLL_REL}>"
            "${OPENSSL_LIBCRYPTO_DLL}"
            "${OPENSSL_LIBSSL_DLL}"
            "$<TARGET_FILE:LibtorrentRasterbar::torrent-rasterbar>"
            "$<TARGET_FILE_DIR:qbt_app>"
        )

        list(APPEND WINDEPLOYQT_OPTIONS_LIST "--verbose" "0")
        add_custom_command(TARGET qbt_app POST_BUILD
            COMMAND Qt6::windeployqt ${WINDEPLOYQT_OPTIONS_LIST} "$<TARGET_FILE:qbt_app>"
            COMMENT "Run windeployqt after build"
        )
    endif()
endif()

# Additional feature dependent configuration
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
if (STACKTRACE)
    target_compile_definitions(qbt_app PRIVATE STACKTRACE)

    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(qbt_app PRIVATE stacktrace_win.h)

        if (GUI)
            target_sources(qbt_app PRIVATE
                stacktracedialog.h
                stacktracedialog.cpp
                stacktracedialog.ui
            )
        endif()

        # i686 arch on Windows requires frame pointer preservation
        if (MSVC)
            target_compile_options(qbt_app PRIVATE /Zi)
            target_link_options(qbt_app PUBLIC LINKER:/DEBUG)
            if (CMAKE_SIZEOF_VOID_P EQUAL 4)
                target_compile_options(qbt_app PRIVATE /Oy)
            endif()
        else()
            if (CMAKE_SIZEOF_VOID_P EQUAL 4)
                target_compile_options(qbt_app PRIVATE -fno-omit-frame-pointer)
            endif()
        endif()

        target_link_libraries(qbt_app PUBLIC dbghelp)
    else()
        target_sources(qbt_app PRIVATE stacktrace.h)
    endif()
endif()

if (GUI)
    target_link_libraries(qbt_app PRIVATE qbt_gui)
    if ((CMAKE_SYSTEM_NAME STREQUAL "Windows") OR (CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
        get_target_property(_QT_LIB_TYPE Qt6::Core TYPE)
        if (_QT_LIB_TYPE STREQUAL "STATIC_LIBRARY")
            # this does nothing on non-static builds, but would still print an unecessary warning
            qt_import_plugins(qbt_app INCLUDE Qt6::QSvgIconPlugin Qt6::QSvgPlugin)
        endif()
    endif()
endif()

if (WEBUI)
    target_sources(qbt_app PRIVATE
        ${QBT_WEBUI_QM_FILES_LIST}
        "${qBittorrent_BINARY_DIR}/src/webui/www/translations/webui_translations.qrc" # yes, it's supposed to be "*_BINARY_DIR"
    )
    target_link_libraries(qbt_app PRIVATE qbt_webui)
endif()

# Installation
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# On Windows, everything goes in the base install location

install(TARGETS qbt_app
    RUNTIME DESTINATION "$<IF:$<BOOL:${WIN32}>,.,${CMAKE_INSTALL_BINDIR}>"
    BUNDLE  DESTINATION "."
    COMPONENT runtime
)

# TODO: works for MSVC, what about MinGW?
if (MSVC)
    install(FILES $<TARGET_PDB_FILE:qbt_app>
        DESTINATION "."
        COMPONENT runtime
        OPTIONAL
    )
    if (MSVC_RUNTIME_DYNAMIC)
        install(
            DIRECTORY "${CMAKE_BINARY_DIR}/" # trailing slash is required here to copy only the contents
            DESTINATION "."
            COMPONENT runtime
            FILES_MATCHING REGEX ".*.dll|.*.qm"
            # the exclusion below is required to prevent creation of empty directories at the destination
            # see https://gitlab.kitware.com/cmake/cmake/-/issues/19189
            REGEX "CMakeFiles.*|dist.*|src.*" EXCLUDE
        )
    endif()
endif()
