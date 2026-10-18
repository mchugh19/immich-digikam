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
# Status: scaffold, CMake side verified against digikam-devel
# 9.1.0. First compile runs in a Fedora container with
# digikam-devel installed (or Christian's Fedora toolbox, which
# carries the same digikam 9.1.0).
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
# Target names and install paths verified against the installed
# digikam-devel 9.1.0 packages (Fedora): the core target is
# Digikam::digikamcore, and DigikamPlugin's config provides
# DPluginGenericInstallPath (/usr/lib64/qt6/plugins/digikam/generic).
find_package(DigikamCore REQUIRED)
find_package(DigikamPlugin REQUIRED)

add_library(generic_immich MODULE
    immichplugin.cpp
    immichsession.cpp
    immichtalker.cpp
    immichwindow.cpp
)

target_link_libraries(generic_immich
    PRIVATE
        Digikam::digikamcore
        Qt6::Widgets
        Qt6::Network
        KF6::ConfigCore
        KF6::I18n
)

install(TARGETS generic_immich
        DESTINATION ${DPluginGenericInstallPath})
