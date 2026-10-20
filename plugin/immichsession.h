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


#ifndef DIGIKAM_IMMICH_SESSION_H
#define DIGIKAM_IMMICH_SESSION_H

// Qt includes

#include <QHash>
#include <QList>
#include <QString>

// Local includes

#include "immichitem.h"

namespace DigikamGenericImmichPlugin
{

/**
 * Connection settings for one Immich server: base URL and API key.
 * The non-secret settings are persisted in digiKam's configuration
 * (group "Immich Settings"). The API key is stored in KWallet when
 * a wallet is available, with the same configuration group kept
 * only as a fallback for systems where the wallet cannot be used.
 *
 * The API key needs these permissions on the Immich server
 * (names as of Immich 3.3.3):
 * asset.upload, asset.read, asset.download, asset.view,
 * album.read, album.create, albumAsset.create.
 */
class ImmichSession
{
public:

    ImmichSession();
    ~ImmichSession();

    QString url()    const;
    QString apiKey() const;

    /** Last folder an album was downloaded to (dialog start). */
    QString downloadDir() const;

    /** What an export does with the previous copy of a photo
     * that was edited in digiKam: "stack" (the new version
     * becomes the primary of a stack over the old one) or
     * "trash" (overwrite: the old copy goes to Immich's trash).
     */
    QString replaceMode() const;

    /** Export destination choice: "library" (no album) or
     * "album" (the album picked in the export window).
     */
    QString exportTargetMode() const;

    void setUrl(const QString& url);
    void setApiKey(const QString& apiKey);
    void setDownloadDir(const QString& dir);
    void setReplaceMode(const QString& mode);
    void setExportTargetMode(const QString& mode);

    void save();
    void load();

    /** Contents listings by source key ("album:<id>" or
     * "tag:<id>"), cached for the session. The cache lives
     * here, not in a tool window, so it survives the windows,
     * which are created on demand and destroy themselves when
     * closed.
     */
    bool                  hasListing(const QString& key) const;
    QList<ImmichAssetRef> listing(const QString& key) const;
    void                  cacheListing(const QString& key,
                                       const QList<ImmichAssetRef>& assets);
    void                  invalidateListing(const QString& key);
    void                  clearListings();

private:

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_SESSION_H
