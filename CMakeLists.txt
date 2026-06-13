cmake_minimum_required(VERSION 3.16)

project(tiBackup VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headless server runtime: Core + Network only, no GUI.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

qt_add_executable(tiBackup
    main.cpp
    diskmain.cpp
    diskmain.h
    diskwatcher.cpp
    diskwatcher.h
    global.cpp
    global.h
    httprequestmapper.cpp
    httprequestmapper.h
    httpserve.cpp
    httpserve.h
    httpctrl/indexctrl.cpp
    httpctrl/indexctrl.h

    # --- vendored QtWebApp: logging ---
    QtWebApp/logging/logglobal.h
    QtWebApp/logging/logmessage.cpp
    QtWebApp/logging/logmessage.h
    QtWebApp/logging/logger.cpp
    QtWebApp/logging/logger.h
    QtWebApp/logging/filelogger.cpp
    QtWebApp/logging/filelogger.h
    QtWebApp/logging/dualfilelogger.cpp
    QtWebApp/logging/dualfilelogger.h

    # --- vendored QtWebApp: httpserver ---
    QtWebApp/httpserver/httpglobal.cpp
    QtWebApp/httpserver/httpglobal.h
    QtWebApp/httpserver/httplistener.cpp
    QtWebApp/httpserver/httplistener.h
    QtWebApp/httpserver/httpconnectionhandler.cpp
    QtWebApp/httpserver/httpconnectionhandler.h
    QtWebApp/httpserver/httpconnectionhandlerpool.cpp
    QtWebApp/httpserver/httpconnectionhandlerpool.h
    QtWebApp/httpserver/httprequest.cpp
    QtWebApp/httpserver/httprequest.h
    QtWebApp/httpserver/httpresponse.cpp
    QtWebApp/httpserver/httpresponse.h
    QtWebApp/httpserver/httpcookie.cpp
    QtWebApp/httpserver/httpcookie.h
    QtWebApp/httpserver/httprequesthandler.cpp
    QtWebApp/httpserver/httprequesthandler.h
    QtWebApp/httpserver/httpsession.cpp
    QtWebApp/httpserver/httpsession.h
    QtWebApp/httpserver/httpsessionstore.cpp
    QtWebApp/httpserver/httpsessionstore.h
    QtWebApp/httpserver/staticfilecontroller.cpp
    QtWebApp/httpserver/staticfilecontroller.h

    # --- vendored QtWebApp: templateengine ---
    QtWebApp/templateengine/templateglobal.h
    QtWebApp/templateengine/template.cpp
    QtWebApp/templateengine/template.h
    QtWebApp/templateengine/templateloader.cpp
    QtWebApp/templateengine/templateloader.h
    QtWebApp/templateengine/templatecache.cpp
    QtWebApp/templateengine/templatecache.h
)

target_include_directories(tiBackup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/QtWebApp/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/QtWebApp/httpserver
    ${CMAKE_CURRENT_SOURCE_DIR}/QtWebApp/templateengine
)

# tiBackupLib: prefer the installed/exported CMake package (CI build-check installs it
# to a prefix and sets CMAKE_PREFIX_PATH). Fall back to building the sibling source tree
# in-tree, so local development needs no separate install step.
find_package(tiBackupLib CONFIG QUIET)
if(NOT tiBackupLib_FOUND)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tiBackupLib/CMakeLists.txt")
        message(STATUS "tiBackupLib package not found - building sibling source tree ../tiBackupLib")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tiBackupLib ${CMAKE_BINARY_DIR}/tiBackupLib-build)
    else()
        message(FATAL_ERROR "tiBackupLib not found. Install the tibackuplib-dev package, set "
            "CMAKE_PREFIX_PATH to an install prefix, or place the tiBackupLib source tree next to this project.")
    endif()
endif()

target_compile_options(tiBackup PRIVATE
    $<$<CONFIG:Debug>:-pipe>
    $<$<CONFIG:Release>:-pipe;-O2>
)

target_link_libraries(tiBackup PRIVATE
    Qt6::Core
    Qt6::Network
    tiBackupLib::tiBackupLib
)

install(TARGETS tiBackup
    RUNTIME DESTINATION /usr/bin
)
