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


#pragma once

// Qt includes

#include <QImage>
#include <QJsonObject>
#include <QPixmap>

class QListWidget;
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

class SyncFields;

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
    /** The one owner of the New Album and Start buttons'
     * enabled state: usable exactly when the window is
     * connected and no transfer is running.
     */
    void refreshActionButtons();
    /** Copy the connection edits into the session; every
     * action entry point starts from what the user sees.
     */
    void syncSessionFromEdits();
    /** Session cache key for a source's contents listing. */
    QString listingKeyFor(const QString& sourceId) const;
    /** Ask the talker for the current source's assets. */
    void listSourceAssets(const QString& sourceId);
    QListWidgetItem* findGridItem(QListWidget* grid, const QString& id) const;
    /** Zero every export-run counter and queue (start of a
     * run). clearExportQueues is the container-and-flag half,
     * shared with the cancel path, which keeps its counters
     * for the cancellation report.
     */
    void resetExportState();
    void clearExportQueues();
    void setListingLoading(bool loading);
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
                       const QString& serverUpdatedAt,
                       const SyncFields& dkFields);
    void nextDownload();
    void continueDownloads();
    void nextMeta();
    bool writeRecordSidecar(const QString& targetFile, const SyncFields& fields);
    void finishDownload();
    QString sidecarFor(const SyncFields& fields, const QString& filePath);
    SyncFields dkFieldsFor(const QUrl& url) const;
    /** Claim a server asset for this selection: appends the id
     * to the album list and returns true on the first claim;
     * a later file resolving to the same asset counts as a
     * duplicate within the selection and returns false.
     */
    bool claimAssetId(const QString& assetId);
    void queueSyncJob(const QString& assetId, const QString& localPath,
                      const SyncFields& dk);
    /** Write a generated sidecar to its temp path and register
     * it for removal when the upload finishes; empty on failure.
     */
    QString writeTempSidecar(const QString& tempPath, const QString& packet);
    /** Record the current identity match as the copy the file
     * at path replaces once its upload lands.
     */
    void markReplace(const QString& path);

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

