cmake_minimum_required(VERSION 3.16)

# No version here on purpose: the version is maintained centrally in
# tiBackupLib (project(... VERSION ...) in ../tiBackupLib/CMakeLists.txt) and
# reaches the server at runtime via the shared, generated config.h
# (tibackup_config::version).
project(tiBackup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Headless server runtime: Core + Network + embedded web UI (HttpServer + WebSockets), no GUI.
find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer WebSockets)

qt_standard_project_setup()

qt_add_executable(tiBackup
    main.cpp
    logging.cpp
    logging.h
    diskmain.cpp
    diskmain.h
    diskwatcher.cpp
    diskwatcher.h

    # --- embedded web UI (QHttpServer + QWebSockets) ---
    webserver/webserver.cpp
    webserver/webserver.h
    webserver/apirouter.cpp
    webserver/apirouter.h
    webserver/wshub.cpp
    webserver/wshub.h
    webserver/jsonmap.cpp
    webserver/jsonmap.h
    webserver/auth/passwordhash.cpp
    webserver/auth/passwordhash.h
    webserver/auth/sessionstore.cpp
    webserver/auth/sessionstore.h
)

target_include_directories(tiBackup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    Qt6::HttpServer
    Qt6::WebSockets
    tiBackupLib::tiBackupLib
)

install(TARGETS tiBackup
    RUNTIME DESTINATION /usr/bin
)
