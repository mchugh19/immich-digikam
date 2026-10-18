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

#ifndef DIGIKAM_IMMICH_TALKER_H
#define DIGIKAM_IMMICH_TALKER_H

// Qt includes

#include <QNetworkRequest>
#include <QObject>

// Local includes

#include "immichitem.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace DigikamGenericImmichPlugin
{

class ImmichSession;

/**
 * Immich API client. One asynchronous call per operation, results
 * delivered by signals, mirroring the endpoints the Python
 * prototype verified against a live server (Immich 3.3.3):
 *
 *   GET  /albums                    list albums
 *   POST /albums                    create an album
 *   PUT  /albums/<id>/assets        add assets to an album
 *   POST /assets                    multipart upload (assetData,
 *                                   optional sidecarData)
 *   POST /search/metadata           an album's assets (3.x does not
 *                                   embed them in the album record)
 *   GET  /assets/<id>               one asset's full record
 *   GET  /assets/<id>/original      download the original file
 *   GET  /assets/<id>/sidecar       download its XMP sidecar (404
 *                                   when the asset has none)
 *
 * Authentication is the x-api-key header on every request.
 */
class ImmichTalker : public QObject
{
    Q_OBJECT

public:

    explicit ImmichTalker(ImmichSession* const session, QObject* const parent = nullptr);
    ~ImmichTalker() override;

    void listAlbums();
    void createAlbum(const QString& name);
    void addToAlbum(const QString& albumId, const QStringList& assetIds);

    /**
     * Upload one file, with its XMP sidecar when sidecarPath names
     * an existing file. Emits signalUploadSucceeded with the asset
     * id and the server status ("created" or "duplicate").
     */
    void uploadAsset(const QString& filePath, const QString& sidecarPath = QString());

    /** List the assets of one album (id + original file name). */
    void listAlbumAssets(const QString& albumId);

    /**
     * Download one asset's original to targetPath, followed by its
     * sidecar to targetPath + ".xmp" when the server has one.
     * Emits signalDownloadFinished either way; the download only
     * fails when the original itself cannot be fetched or written.
     */
    void downloadAsset(const QString& assetId, const QString& targetPath);

    void cancel();

Q_SIGNALS:

    void signalBusy(bool val);
    void signalError(const QString& msg);
    void signalAlbums(const QList<ImmichAlbum>& albumList);
    void signalAlbumCreated(const ImmichAlbum& album);
    void signalAlbumUpdated();
    void signalUploadSucceeded(const QString& assetId, const QString& status);
    void signalAlbumAssets(const QList<ImmichAssetRef>& assets);
    void signalDownloadFinished(const QString& assetId, bool ok);

private Q_SLOTS:

    void slotFinished(QNetworkReply* reply);

private:

    QNetworkRequest makeRequest(const QString& path) const;
    void sendJson(const QString& method, const QString& path, const QByteArray& body);

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_TALKER_H
