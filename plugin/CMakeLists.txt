#
# immich-digikam: a digiKam generic plugin to sync albums with a
# self-hosted Immich server (pull originals into digiKam, push
# finished files back, metadata round-tripping through XMP sidecars).
#
# The class layout mirrors digiKam's own Piwigo web-service plugin
# (plugin / session / talker / window), with Immich's API in place of
# Piwigo's: server URL + API key instead of a login session, JSON
# over HTTPS, multipart uploads with a sidecarData part.
#
# Status: scaffold. Written against the digiKam plugin API as of
# digiKam master (Qt6/KF6) but not yet compiled: the first build task
# is to build it in a Fedora container with digikam-devel installed,
# confirm the exported DigikamCore CMake targets and the plugin
# install path below, and pin the minimum digiKam version to the one
# in daily use.
#

cmake_minimum_required(VERSION 3.16)
project(digikam-immich-plugin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# KF6 ships no umbrella config; each framework is its own package.
find_package(KF6Config REQUIRED)
find_package(KF6I18n REQUIRED)

# Provides the DPluginGeneric headers and the digiKam core library.
# TODO(first build): confirm package/target names from the installed
# DigikamCoreConfig.cmake and adjust.
find_package(DigikamCore REQUIRED)

add_library(generic_immich MODULE
    immichplugin.cpp
    immichsession.cpp
    immichtalker.cpp
    immichwindow.cpp
)

target_link_libraries(generic_immich
    PRIVATE
        Digikam::core
        Qt6::Widgets
        Qt6::Network
        KF6::ConfigCore
        KF6::I18n
)

# TODO(first build): confirm the generic-plugin install directory
# for the target digiKam version.
include(GNUInstallDirs)
install(TARGETS generic_immich
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/plugins/digikam/generic)
