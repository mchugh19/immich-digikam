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

#include <QJsonArray>
#include <QJsonObject>
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
 * delivered by signals. Endpoints verified against a live server
 * (Immich 3.3.3):
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
 *   POST /assets/bulk-upload-check  which of a set of SHA-1
 *                                   checksums the server already
 *                                   has (action reject = duplicate)
 *   PUT  /assets/<id>               metadata sync (rating,
 *                                   description, GPS) for an
 *                                   already-uploaded asset
 *   POST /tags                      create a tag
 *   PUT  /tags/assets               assign a tag to assets
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

    /** List the server's tags. Emits signalTags. */
    void listTags();

    /** List the assets of one album (id + original file name). */
    void listAlbumAssets(const QString& albumId);

    /** List the assets carrying one tag. Emits signalAlbumAssets,
     * like listAlbumAssets: the search endpoint takes a tagIds
     * filter in place of albumIds.
     */
    void listTagAssets(const QString& tagId);

    /** Fetch one asset's preview thumbnail. Emits signalThumbnail
     * with the image bytes (empty when unavailable).
     */
    void fetchThumbnail(const QString& assetId);

    /**
     * Download one asset's original to targetPath, followed by its
     * sidecar to targetPath + ".xmp" when the server has one. When
     * the server has none, the asset record is fetched instead and
     * emitted as signalAssetRecord for sidecar generation.
     * Emits signalDownloadFinished either way; the download only
     * fails when the original itself cannot be fetched or written.
     */
    void downloadAsset(const QString& assetId, const QString& targetPath);

    /**
     * Refresh only the sidecar of an asset whose file is already on
     * disk: fetch the server sidecar, or, when there is none, the
     * asset record (emitted as signalMetaRecord for generation).
     * Emits signalMetaFinished either way.
     */
    void downloadSidecarOnly(const QString& assetId, const QString& targetPath);

    /**
     * Ask the server which of the given files (local path -> base64
     * SHA-1 of the contents) it already holds. Emits
     * signalUploadCheck with the duplicates only: local path ->
     * existing asset id.
     */
    void checkUploads(const QMap<QString, QString>& checksumsByPath);

    /** Fetch one asset's record for the metadata sync.
     * Emits signalSyncRecord with the HTTP status (0 on a
     * network-level failure).
     */
    void fetchAssetForSync(const QString& assetId);

    /** Apply metadata changes to an existing asset
     * (PUT /assets/<id>). Emits signalAssetPatched with the
     * HTTP status.
     */
    void updateAsset(const QString& assetId, const QJsonObject& body);

    /** Create a tag by value. Emits signalTagCreated with the
     * new tag's id (empty on failure) and the HTTP status.
     */
    void createTag(const QString& name);

    /** Assign one tag to a set of assets (PUT /tags/assets).
     * Emits signalBulkTagged with the HTTP status.
     */
    void bulkTagAssets(const QString& tagId, const QStringList& assetIds);

    /** Search assets by original file name, for the export
     * identity match. Emits signalIdentitySearch with the raw
     * result items (id, name, capture time, dimensions). When
     * albumId is given the search is scoped to that album:
     * the file name is the primary key within the album the
     * user is exporting to.
     */
    void searchByFileName(const QString& name, const QString& albumId = QString());

    /** Create a stack (POST /stacks); the first asset id
     * becomes the primary. Emits signalStackCreated with the
     * first asset id, the new stack id and the HTTP status.
     */
    void createStack(const QStringList& assetIds);

    /** Fetch one stack's member ids (GET /stacks/<id>).
     * Emits signalStackFetched with the HTTP status.
     */
    void fetchStack(const QString& stackId);

    /** List the albums containing one asset (GET
     * /albums?assetId=). Emits signalAlbumsForAsset.
     */
    void listAlbumsForAsset(const QString& assetId);

    /** Move assets to the trash (DELETE /assets). Emits
     * signalAssetsDeleted with the HTTP status.
     */
    void deleteAssets(const QStringList& assetIds);

    void cancel();

Q_SIGNALS:

    void signalBusy(bool val);
    void signalError(const QString& msg);
    void signalAlbums(const QList<ImmichAlbum>& albumList);
    void signalTags(const QList<ImmichTag>& tagList);
    void signalThumbnail(const QString& assetId, const QByteArray& bytes);
    void signalAlbumCreated(const ImmichAlbum& album);
    void signalAlbumUpdated(bool changed);
    void signalUploadSucceeded(const QString& assetId, const QString& status);
    void signalAlbumAssets(const QList<ImmichAssetRef>& assets);
    void signalDownloadFinished(const QString& assetId, bool ok);
    /** The asset downloaded but the server holds no sidecar file
     * for it: the full asset record, so the window can generate a
     * sidecar from the Immich metadata. The window completes the
     * item itself after writing it.
     */
    void signalAssetRecord(const QString& assetId, const QString& targetFile,
                           const QJsonObject& record);
    /** A metadata-only refresh finished; updated says whether a
     * sidecar was (re)written.
     */
    void signalMetaFinished(const QString& assetId, bool updated);
    /** The record behind a metadata-only refresh, for sidecar
     * generation (the metadata counterpart of signalAssetRecord).
     */
    void signalMetaRecord(const QString& assetId, const QString& targetFile,
                          const QJsonObject& record);
    void signalUploadCheck(const QMap<QString, QString>& duplicatesByPath);
    void signalSyncRecord(const QString& assetId, const QJsonObject& record, int httpStatus);
    void signalAssetPatched(const QString& assetId, int httpStatus);
    void signalTagCreated(const QString& name, const QString& id, int httpStatus);
    void signalBulkTagged(const QString& tagId, int httpStatus);
    void signalIdentitySearch(const QString& name, const QJsonArray& items);
    void signalStackCreated(const QString& firstAssetId, const QString& stackId, int httpStatus);
    void signalStackFetched(const QString& stackId, const QStringList& memberIds, int httpStatus);
    void signalAlbumsForAsset(const QString& assetId, const QStringList& albumIds);
    void signalAssetsDeleted(int httpStatus);

private Q_SLOTS:

    void slotFinished(QNetworkReply* reply);

private:

    QNetworkRequest makeRequest(const QString& path) const;
    void sendJson(const QString& method, const QString& path, const QByteArray& body);
    void fetchListingPage(int page);

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_TALKER_H
