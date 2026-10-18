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

#ifndef DIGIKAM_IMMICH_SESSION_H
#define DIGIKAM_IMMICH_SESSION_H

// Qt includes

#include <QString>

namespace DigikamGenericImmichPlugin
{

/**
 * Connection settings for one Immich server: base URL and API key.
 * Persisted in digiKam's configuration (group "Immich Settings"),
 * the same way digiKam's other web-service plugins keep theirs.
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

    void setUrl(const QString& url);
    void setApiKey(const QString& apiKey);
    void setDownloadDir(const QString& dir);

    void save();
    void load();

private:

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_SESSION_H
