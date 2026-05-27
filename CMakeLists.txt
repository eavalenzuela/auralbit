cmake_minimum_required(VERSION 3.21)
project(auralbit LANGUAGES C CXX VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-unused-parameter)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(LIBMTP REQUIRED IMPORTED_TARGET libmtp)

add_subdirectory(third_party)
add_subdirectory(src)

# ---- Packaging (CPack → .deb) ----
# Produces a Debian/Ubuntu binary package via:
#   cmake --build build --target package
set(CPACK_PACKAGE_NAME "auralbit")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Compact Linux GUI music manager and player")
set(CPACK_PACKAGE_DESCRIPTION
    "auralbit is a small music manager + player intended to live in 1/4 of the
 screen. Think GUI cmus rather than Rhythmbox. Supports common music
 formats, playlists with import/export, sync to mounted devices, a live
 spectrum visualiser, and full MPRIS integration.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eavalenzuela/auralbit")
set(CPACK_PACKAGE_CONTACT "eavalenzuela7@gmail.com")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
# Resolve the exact runtime libs (Qt6, TagLib, glibc, …) automatically.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
