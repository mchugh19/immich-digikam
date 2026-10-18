#
# SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Installs the dk-immich theme icons (16/22/32/48 PNG plus the
# scalable SVGZ), in the form the in-tree web-service plugins
# use. Only the upstream build (../upstream/CMakeLists.txt)
# adds this directory; the standalone build embeds the SVG in
# the binary instead, so it needs no install rule to show its
# icon.

file(GLOB _icons_files *.png *.svgz)

ecm_install_icons(ICONS       ${_icons_files}
                  DESTINATION ${KDE_INSTALL_FULL_ICONDIR}
)
