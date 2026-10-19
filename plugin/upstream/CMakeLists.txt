#
# SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
#
# SPDX-License-Identifier: BSD-3-Clause
#
# In-tree build file for digiKam (graphics/digikam,
# core/dplugins/generic/webservices/immich/). See README.md in
# this directory for the drop-in steps. The standalone build in
# the parent directory is what this repository builds day to
# day; this file exists so the plugin can be lifted into the
# digiKam tree without a build-system rewrite.

APPLY_COMMON_POLICIES()

include(MacroDPlugins)

include_directories($<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:Qt6::Core,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:Qt6::Network,INTERFACE_INCLUDE_DIRECTORIES>

                    $<TARGET_PROPERTY:KF6::ConfigCore,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:KF6::I18n,INTERFACE_INCLUDE_DIRECTORIES>
                    $<TARGET_PROPERTY:KF6::Wallet,INTERFACE_INCLUDE_DIRECTORIES>
)

set(immichplugin_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/immichplugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/immichsession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/immichtalker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/immichwindow.cpp
)

# The icon is embedded in the binary (the plugin's icon()
# returns it from the resource), so a build without the theme
# install still shows it.
qt_add_resources(immichplugin_RCC_SRCS
                 ${CMAKE_CURRENT_SOURCE_DIR}/immich_icons.qrc
)

DIGIKAM_ADD_GENERIC_PLUGIN(NAME    Immich
                           SOURCES ${immichplugin_SRCS} ${immichplugin_RCC_SRCS}
                           DEPENDS KF6::Wallet
)

add_subdirectory(icons)
