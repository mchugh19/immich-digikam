#!/bin/sh
#
# SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Extract translatable strings from the plugin sources into
# po/immich-digikam.pot, following the KDE convention (the
# in-tree digiKam build drives the same script shape from its
# own Messages.sh). Requires extractrc and xgettext from the
# KDE translation tooling (or gettext with the KDE keywords).

BASEDIR="."
PROJECT="immich-digikam"

cd "$BASEDIR" || exit 1
mkdir -p po

find . -name '*.cpp' -o -name '*.h' | sort > po/infiles.list

xgettext --from-code=UTF-8 -C -kde -ci18n -ki18n:1 -ki18nc:1c,2 \
         -ki18np:1,2 -ki18ncp:1c,2,3 -ktr2i18n:1 -kI18N_NOOP:1 \
         -kI18N_NOOP2:1c,2 --files-from=po/infiles.list \
         -D "$BASEDIR" -o "po/$PROJECT.pot" || exit 1

rm -f po/infiles.list
