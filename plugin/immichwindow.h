/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * Description : a digiKam plugin to sync albums with an Immich
 *               web-service (pull originals, push finished files,
 *               metadata round-trips through XMP sidecars).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2, or (at your option) any later version.
 *
 * ============================================================ */

#ifndef DIGIKAM_IMMICH_WINDOW_H
#define DIGIKAM_IMMICH_WINDOW_H

// Qt includes

#include <QDialog>
#include <QJsonObject>

// Local includes

#include "immichitem.h"

namespace Digikam
{
class DInfoInterface;
class DPlugin;
}

using namespace Digikam;

namespace DigikamGenericImmichPlugin
{

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
class ImmichWindow : public QDialog
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

    explicit ImmichWindow(DInfoInterface* const iface, Mode mode,
                          QWidget* const parent = nullptr);
    ~ImmichWindow() override;

    void setPlugin(DPlugin* const plugin);

private Q_SLOTS:

    void slotSaveSettings();
    void slotRefreshAlbums();
    void slotAlbums(const QList<ImmichAlbum>& albums);
    void slotNewAlbum();
    void slotAlbumCreated(const ImmichAlbum& album);
    void slotUploadSelection();
    void slotUploadSucceeded(const QString& assetId, const QString& status);
    void slotUploadCheck(const QMap<QString, QString>& duplicatesByPath);
    void slotAlbumUpdated();
    void slotDownloadAlbum();
    void slotAlbumAssets(const QList<ImmichAssetRef>& assets);
    void slotDownloadFinished(const QString& assetId, bool ok);
    void slotAssetRecord(const QString& assetId, const QString& targetFile,
                         const QJsonObject& record);
    void slotCancelTransfer();
    void slotError(const QString& msg);
    void slotBusy(bool val);

private:

    void setTransferButtonsEnabled(bool enabled);
    void nextUpload();
    void finishUpload();
    void nextDownload();
    void finishDownload();
    QString sidecarFor(const QUrl& url, const QString& filePath);

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_WINDOW_H
