/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * Date        : 2026-10-18
 *
 * SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ============================================================ */


#pragma once

// Qt includes

#include <QString>

namespace DigikamGenericImmichPlugin
{

/** One Immich album, as returned by GET /albums. startDate and
 * endDate are the capture dates of the album's oldest and newest
 * assets (the date range Immich's own album view shows).
 */
class ImmichAlbum
{
public:
    QString id;
    QString name;
    int     assetCount = 0;
    QString startDate;
    QString endDate;
    QString thumbnailAssetId;
};

/** One Immich tag, as returned by GET /tags. */
class ImmichTag
{
public:
    QString id;
    QString name;
};

/** One asset of an album, as listed by POST /search/metadata. */
class ImmichAssetRef
{
public:
    QString id;
    QString fileName;
    QString fileCreatedAt;
};

} // namespace DigikamGenericImmichPlugin

