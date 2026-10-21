/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * Date        : 2026-10-18
 * Description : a digiKam plugin to sync albums with an Immich
 *               web-service (pull originals, push finished files,
 *               metadata round-trips through XMP sidecars).
 *
 * SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ============================================================ */


#ifndef DIGIKAM_IMMICH_WINDOW_H
#define DIGIKAM_IMMICH_WINDOW_H

// Qt includes

#include <QImage>
#include <QJsonObject>
#include <QPixmap>

class QListWidgetItem;
class QShowEvent;

// Local includes

#include "immichitem.h"
#include "wstooldialog.h"

namespace Digikam
{
class DInfoInterface;
class LoadingDescription;
class ThumbnailLoadThread;
}

using namespace Digikam;

namespace DigikamGenericImmichPlugin
{

struct SyncFields;

class ImmichSession;
class ImmichTalker;

/**
 * Tool dialog: connection settings (server URL + API key), the
 * server's album list, and the transfer actions. Upload sends the
 * items currently selected in digiKam, each with its XMP sidecar:
 * an on-disk sidecar when one exists next to the file, otherwise a
 * sidecar generated from the item's metadata in digiKam's database
 * (tags, rating, caption, GPS), otherwise the file's embedded XMP
 * packet. When an album is selected in the list, the uploaded
 * items are added to it. Download writes the selected album's
 * originals and their sidecars into a chosen folder, skipping
 * files that are already there.
 */
class ImmichWindow : public WSToolDialog
{
    Q_OBJECT

public:

    /** Which tool this dialog serves. digiKam separates import and
     * export tools; the dialog is shared and shows only its own
     * direction's actions.
     */
    enum class Mode
    {
        Export,
        Import
    };

    explicit ImmichWindow(DInfoInterface* const iface,
                          ImmichSession* const session, Mode mode,
                          QWidget* const parent = nullptr);
    ~ImmichWindow() override;

protected:

    void showEvent(QShowEvent* event) override;

private Q_SLOTS:

    void slotSaveSettings();
    void slotConnect();
    void slotRefreshAlbums();
    void slotAlbums(const QList<ImmichAlbum>& albums);
    void slotTags(const QList<ImmichTag>& tags);
    void slotSourceChanged();
    void slotFilterChanged(const QString& text);
    void slotPreviewSortChanged();
    void slotThumbnail(const QString& assetId, const QByteArray& bytes);
    void slotLocalThumbnailLoaded(const LoadingDescription& desc, const QPixmap& pix);
    void slotNewAlbum();
    void slotAlbumCreated(const ImmichAlbum& album);
    void slotUploadSelection();
    void slotUploadSucceeded(const QString& assetId, const QString& status);
    void slotUploadCheck(const QMap<QString, QString>& duplicatesByPath);
    void nextIdentity();
    void slotIdentitySearch(const QString& name, const QJsonArray& items);
    void slotIdentityThumb(const QString& assetId, const QByteArray& bytes);
    void startRestoreOrSync();
    void restoreNext();
    void restoreAdvanceAdd();
    void restoreStackStep();
    void slotStackFetched(const QString& stackId, const QStringList& memberIds, int httpStatus);
    void slotStackCreated(const QString& firstAssetId, const QString& stackId, int httpStatus);
    void slotRestoreAlbums(const QString& assetId, const QStringList& albumIds);
    void slotAssetsDeleted(int httpStatus);
    void slotAlbumUpdated(bool changed);
    void slotDownloadAlbum();
    void slotAlbumAssets(const QList<ImmichAssetRef>& assets);
    void slotDownloadFinished(const QString& assetId, bool ok);
    void slotAssetRecord(const QString& assetId, const QString& targetFile,
                         const QJsonObject& record);
    void slotMetaFinished(const QString& assetId, bool updated);
    void slotMetaRecord(const QString& assetId, const QString& targetFile,
                        const QJsonObject& record);
    void slotCancelTransfer();
    void slotError(const QString& msg);
    void slotSyncRecord(const QString& assetId, const QJsonObject& record, int httpStatus);
    void slotAssetPatched(const QString& assetId, int httpStatus);
    void slotTagCreated(const QString& name, const QString& id, int httpStatus);
    void slotBulkTagged(const QString& tagId, int httpStatus);

private:

    void setTransferButtonsEnabled(bool enabled);
    void setConnected(bool connected);
    void updateConnectionUi();
    void setAlbumLoading(bool loading);
    void applyFilter();
    QListWidgetItem* findAlbumItem(const QString& id) const;
    QString currentSourceId() const;
    QString currentSourceName() const;
    void addAlbumTile(const QString& id, const QString& name,
                      const QString& countText, const QString& rangeText,
                      const QString& thumbnailAssetId);
    void setAlbumCover(QListWidgetItem* item, const QPixmap& pix);
    void refreshTileStyles();
    void selectSource(const QString& id);
    void queueThumb(bool cover, const QString& key, const QString& assetId);
    void pumpThumbs();
    void dropThumbJobs(bool covers);
    void startPreview(const QString& sourceId);
    void fillPreview();
    void fillUploadPreview();
    void finishIdentityCheck(const QImage& localImage, const QByteArray& bytes);
    void updateExportStatus();
    void nextUpload();
    void finishUpload();
    void applyTargetMode();
    void updateTargetLabel();
    void printUploadSummary();
    void maybeStartSync();
    void pumpSyncFetches();
    void finishSyncAsset(const QString& assetId);
    void checkSyncPhaseDone();
    void startSyncTags();
    void handleSyncTagList(const QList<ImmichTag>& tags);
    void nextTagCreate();
    void applyNextBulkTag();
    void finishSync();
    void storeSnapshot(const QString& assetId, const SyncFields& serverFields,
                       const QString& serverUpdatedAt, const QString& checksum,
                       const SyncFields& dkFields);
    void nextDownload();
    void continueDownloads();
    void nextMeta();
    bool writeRecordSidecar(const QString& targetFile, const QJsonObject& record);
    void finishDownload();
    QString sidecarFor(const QUrl& url, const QString& filePath);
    SyncFields dkFieldsFor(const QUrl& url) const;

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_WINDOW_H
