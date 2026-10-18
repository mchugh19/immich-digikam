/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2, or (at your option) any later version.
 *
 * ============================================================ */

#ifndef DIGIKAM_IMMICH_ITEM_H
#define DIGIKAM_IMMICH_ITEM_H

// Qt includes

#include <QString>

namespace DigikamGenericImmichPlugin
{

/** One Immich album, as returned by GET /albums. */
struct ImmichAlbum
{
    QString id;
    QString name;
    int     assetCount = 0;
};

/** One Immich tag, as returned by GET /tags. */
struct ImmichTag
{
    QString id;
    QString name;
};

/** One asset of an album, as listed by POST /search/metadata. */
struct ImmichAssetRef
{
    QString id;
    QString fileName;
    QString fileCreatedAt;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_ITEM_H
