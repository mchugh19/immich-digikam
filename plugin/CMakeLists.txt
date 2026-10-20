#
# SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

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
# Status: working plugin (loads in digiKam 9.1.0, album list,
# upload and download verified against a live Immich 3.3.3).
# Builds in a Fedora container with digikam-devel installed, or
# in a Fedora toolbox carrying the same digikam 9.1.0; see
# README.md in this directory for the recipes and the
# DK_PLUGIN_PATH warning.
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
find_package(KF6Wallet REQUIRED)
find_package(KF6KIO REQUIRED)

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
    immichwindow_albums.cpp
    immichwindow_thumbs.cpp
    immichwindow_upload.cpp
    immichwindow_identity.cpp
    immichwindow_restore.cpp
    immichwindow_sync.cpp
    immichwindow_download.cpp
)

# Embed the plugin icons in the binary: the actions and the
# plugin listing use them, and they must work when the .so is
# copied into place by hand (the theme install rule below then
# never runs). The .qrc is shared with the in-tree build file
# under upstream/.
qt_add_resources(immich_RCC_SRCS immich_icons.qrc)
target_sources(generic_immich PRIVATE ${immich_RCC_SRCS})

target_link_libraries(generic_immich
    PRIVATE
        Digikam::digikamcore
        Qt6::Widgets
        Qt6::Network
        KF6::ConfigCore
        KF6::I18n
        KF6::Wallet
        KF6::KIOWidgets
)

install(TARGETS generic_immich
        DESTINATION ${DPluginGenericInstallPath})

# Theme icon (dk-immich), looked up by the plugin's icon().
include(GNUInstallDirs)
install(FILES icons/dk-immich.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
