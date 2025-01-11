cmake_minimum_required(VERSION 3.10)
project(backup-qt CXX)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig)
find_package(Qt5 COMPONENTS Core)

pkg_check_modules(QT REQUIRED Qt5Core)
pkg_check_modules(GIO REQUIRED gio-2.0)

add_library(backup-qt SHARED src/backup.cc src/backup.h)
target_link_libraries(backup-qt PUBLIC ${QT_LIBRARIES} ${GIO_LIBRARIES})
target_include_directories(backup-qt PUBLIC ${QT_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
target_compile_options(backup-qt PUBLIC -fPIC -DQT_NO_KEYWORDS)

add_executable(file-new example/file-new.cc)
target_compile_options(file-new PUBLIC -Wl,rpath=${CMAKE_BINARY_DIR}/)
target_link_libraries(file-new PUBLIC ${QT_LIBRARIES} ${GIO_LIBRARIES} backup-qt)
target_include_directories(file-new PUBLIC ${GIO_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)
