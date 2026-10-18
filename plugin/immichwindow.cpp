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

// Self includes

#include "immichwindow.h"

// Qt includes

#include <cmath>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

// Local includes

#include "dinfointerface.h"
#include "dmetadata.h"
#include "immichsession.h"
#include "immichtalker.h"

namespace DigikamGenericImmichPlugin
{

class ImmichWindow::Private
{
public:

    DInfoInterface* iface   = nullptr;
    ImmichSession*  session = nullptr;
    ImmichTalker*   talker  = nullptr;

    QLineEdit*    urlEdit     = nullptr;
    QLineEdit*    keyEdit     = nullptr;
    QListWidget*  albumList   = nullptr;
    QLabel*       targetLabel = nullptr;
    QLabel*       status      = nullptr;
    QPushButton*  refreshBtn  = nullptr;
    QPushButton*  newAlbumBtn = nullptr;
    QPushButton*  uploadBtn   = nullptr;
    QPushButton*  downloadBtn = nullptr;
    QPushButton*  cancelBtn   = nullptr;
    QProgressBar* progress    = nullptr;

    // Album to select after the next list refresh (a just-created
    // album, or simply the selection surviving a refresh).
    QString      pendingSelectId;
    QList<QUrl>  pending;
    QStringList  uploadedIds;
    QString      targetAlbumId;
    QString      tempSidecar;
    int          total       = 0;
    int          done        = 0;
    int          failed      = 0;
    int          alreadyThere = 0;
    bool         uploading   = false;
    bool         checking    = false;
    bool         cancelling  = false;

    // Download queue state.
    QList<ImmichAssetRef> dlPending;
    QString      dlDir;
    int          dlTotal       = 0;
    int          dlDone        = 0;
    int          dlDownloaded  = 0;
    int          dlSkipped     = 0;
    int          dlFailed      = 0;
    bool         downloading   = false;
    bool         dlListing     = false;
};

ImmichWindow::ImmichWindow(DInfoInterface* const iface, QWidget* const parent)
    : QDialog(parent),
      d(new Private)
{
    d->iface   = iface;
    d->session = new ImmichSession;
    d->session->load();
    d->talker  = new ImmichTalker(d->session, this);

    setWindowTitle(QStringLiteral("Immich"));

    auto* const form = new QFormLayout;
    d->urlEdit = new QLineEdit(d->session->url());
    d->urlEdit->setPlaceholderText(QStringLiteral("https://photos.example.com"));
    d->keyEdit = new QLineEdit(d->session->apiKey());
    d->keyEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Server URL:"), d->urlEdit);
    form->addRow(QStringLiteral("API key:"), d->keyEdit);

    auto* const saveBtn = new QPushButton(QStringLiteral("Save settings"));
    d->refreshBtn       = new QPushButton(QStringLiteral("Refresh albums"));
    d->newAlbumBtn      = new QPushButton(QStringLiteral("New album..."));
    d->uploadBtn        = new QPushButton(QStringLiteral("Upload selection"));
    d->downloadBtn      = new QPushButton(QStringLiteral("Download album"));
    d->cancelBtn        = new QPushButton(QStringLiteral("Cancel transfer"));
    d->cancelBtn->setVisible(false);
    d->albumList        = new QListWidget;
    d->targetLabel      = new QLabel(QStringLiteral("Upload target: no album (library only)"));
    d->progress         = new QProgressBar;
    d->progress->setVisible(false);
    d->status           = new QLabel;

    auto* const layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(saveBtn);
    layout->addWidget(d->refreshBtn);
    layout->addWidget(d->newAlbumBtn);
    layout->addWidget(d->albumList);
    layout->addWidget(d->targetLabel);
    layout->addWidget(d->uploadBtn);
    layout->addWidget(d->downloadBtn);
    layout->addWidget(d->progress);
    layout->addWidget(d->cancelBtn);
    layout->addWidget(d->status);

    connect(saveBtn, &QPushButton::clicked, this, &ImmichWindow::slotSaveSettings);
    connect(d->refreshBtn, &QPushButton::clicked, this, &ImmichWindow::slotRefreshAlbums);
    connect(d->newAlbumBtn, &QPushButton::clicked, this, &ImmichWindow::slotNewAlbum);
    connect(d->talker, &ImmichTalker::signalAlbumCreated, this, &ImmichWindow::slotAlbumCreated);
    connect(d->uploadBtn, &QPushButton::clicked, this, &ImmichWindow::slotUploadSelection);
    connect(d->albumList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*)
            {
                d->targetLabel->setText(current
                    ? QStringLiteral("Upload target: %1").arg(current->data(Qt::UserRole + 1).toString())
                    : QStringLiteral("Upload target: no album (library only)"));
            });
    connect(d->downloadBtn, &QPushButton::clicked, this, &ImmichWindow::slotDownloadAlbum);
    connect(d->cancelBtn, &QPushButton::clicked, this, &ImmichWindow::slotCancelTransfer);
    connect(d->talker, &ImmichTalker::signalAlbums, this, &ImmichWindow::slotAlbums);
    connect(d->talker, &ImmichTalker::signalAlbumAssets, this, &ImmichWindow::slotAlbumAssets);
    connect(d->talker, &ImmichTalker::signalDownloadFinished, this, &ImmichWindow::slotDownloadFinished);
    connect(d->talker, &ImmichTalker::signalAlbumUpdated, this, &ImmichWindow::slotAlbumUpdated);
    connect(d->talker, &ImmichTalker::signalUploadSucceeded, this, &ImmichWindow::slotUploadSucceeded);
    connect(d->talker, &ImmichTalker::signalUploadCheck, this, &ImmichWindow::slotUploadCheck);
    connect(d->talker, &ImmichTalker::signalError, this, &ImmichWindow::slotError);
    connect(d->talker, &ImmichTalker::signalBusy, this, &ImmichWindow::slotBusy);
}

ImmichWindow::~ImmichWindow()
{
    delete d->session;
    delete d;
}

void ImmichWindow::setPlugin(DPlugin* const /*plugin*/)
{
    // Kept for parity with digiKam's other web-service windows,
    // which receive the plugin instance here.
}

void ImmichWindow::slotSaveSettings()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->session->save();
    d->status->setText(QStringLiteral("Settings saved."));
}

void ImmichWindow::slotRefreshAlbums()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->talker->listAlbums();
}

void ImmichWindow::setTransferButtonsEnabled(bool enabled)
{
    d->refreshBtn->setEnabled(enabled);
    d->newAlbumBtn->setEnabled(enabled);
    d->uploadBtn->setEnabled(enabled);
    d->downloadBtn->setEnabled(enabled);
}

void ImmichWindow::slotNewAlbum()
{
    if (d->uploading || d->downloading)
    {
        return;
    }

    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());

    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New album"),
                                               QStringLiteral("Album name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();

    if (!ok || name.isEmpty())
    {
        return;
    }

    d->status->setText(QStringLiteral("Creating album..."));
    d->talker->createAlbum(name);
}

void ImmichWindow::slotAlbumCreated(const ImmichAlbum& album)
{
    // Refresh the list and land the selection on the new album,
    // ready to be the upload target.
    d->pendingSelectId = album.id;
    d->status->setText(QStringLiteral("Album '%1' created.").arg(album.name));
    d->talker->listAlbums();
}

void ImmichWindow::slotAlbums(const QList<ImmichAlbum>& albums)
{
    QListWidgetItem* const previous = d->albumList->currentItem();
    const QString restoreId = !d->pendingSelectId.isEmpty() ? d->pendingSelectId
                              : (previous ? previous->data(Qt::UserRole).toString()
                                          : QString());
    d->pendingSelectId.clear();

    d->albumList->clear();

    QListWidgetItem* selectItem = nullptr;

    for (const ImmichAlbum& album : albums)
    {
        auto* const item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(album.name).arg(album.assetCount), d->albumList);
        item->setData(Qt::UserRole, album.id);
        item->setData(Qt::UserRole + 1, album.name);

        if (album.id == restoreId)
        {
            selectItem = item;
        }
    }

    if (selectItem)
    {
        d->albumList->setCurrentItem(selectItem);
    }

    d->status->setText(QStringLiteral("%1 albums").arg(albums.size()));
}

void ImmichWindow::slotUploadSelection()
{
    if (d->uploading || d->downloading)
    {
        return;
    }

    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());

    const QList<QUrl> items = d->iface ? d->iface->currentSelectedItems()
                                       : QList<QUrl>();

    if (items.isEmpty())
    {
        d->status->setText(QStringLiteral("No items selected in digiKam."));
        return;
    }

    QListWidgetItem* const albumItem = d->albumList->currentItem();
    d->targetAlbumId = albumItem ? albumItem->data(Qt::UserRole).toString()
                                 : QString();

    d->pending     = items;
    d->uploadedIds.clear();
    d->total       = items.size();
    d->done        = 0;
    d->failed      = 0;
    d->alreadyThere = 0;
    d->uploading   = true;

    setTransferButtonsEnabled(false);
    d->progress->setRange(0, d->total);
    d->progress->setValue(0);
    d->progress->setVisible(true);
    d->cancelBtn->setVisible(true);

    // Ask the server which of these files it already has, so they
    // are skipped instead of re-uploaded.
    QMap<QString, QString> checksums;

    for (const QUrl& url : items)
    {
        const QString path = url.toLocalFile();
        QFile file(path);

        if (!path.isEmpty() && file.open(QIODevice::ReadOnly))
        {
            QCryptographicHash hash(QCryptographicHash::Sha1);

            if (hash.addData(&file))
            {
                checksums.insert(path, QString::fromLatin1(hash.result().toBase64()));
            }
        }
    }

    if (!checksums.isEmpty())
    {
        d->checking = true;
        d->status->setText(QStringLiteral("Checking which files are already on the server..."));
        d->talker->checkUploads(checksums);
        return;
    }

    nextUpload();
}

void ImmichWindow::slotUploadCheck(const QMap<QString, QString>& duplicatesByPath)
{
    d->checking = false;

    QList<QUrl> remaining;

    for (const QUrl& url : d->pending)
    {
        const QString assetId = duplicatesByPath.value(url.toLocalFile());

        if (assetId.isEmpty())
        {
            remaining.append(url);
        }
        else
        {
            // Already on the server: count it, and keep its id so
            // a target album still gets the asset assigned.
            d->uploadedIds.append(assetId);
            ++d->alreadyThere;
            ++d->done;
        }
    }

    d->pending = remaining;
    d->progress->setValue(d->done);
    nextUpload();
}

void ImmichWindow::nextUpload()
{
    if (d->pending.isEmpty())
    {
        finishUpload();
        return;
    }

    const QUrl url        = d->pending.takeFirst();
    const QString path    = url.toLocalFile();
    const QFileInfo info(path);

    if (path.isEmpty() || !info.isFile())
    {
        ++d->failed;
        ++d->done;
        d->progress->setValue(d->done);
        nextUpload();
        return;
    }

    d->status->setText(QStringLiteral("Uploading %1 (%2 of %3)")
                           .arg(info.fileName())
                           .arg(d->done + 1)
                           .arg(d->total));

    d->talker->uploadAsset(path, sidecarFor(url, path));
}

void ImmichWindow::slotCancelTransfer()
{
    if (!d->uploading && !d->downloading)
    {
        return;
    }

    d->cancelling = true;
    d->pending.clear();
    d->dlPending.clear();
    d->status->setText(QStringLiteral("Cancelling..."));
    d->talker->cancel();
}

void ImmichWindow::slotUploadSucceeded(const QString& assetId, const QString& /*status*/)
{
    if (d->cancelling)
    {
        finishUpload();
        return;
    }

    if (!d->uploading)
    {
        return;
    }

    // Duplicates count too: the asset exists on the server, so it
    // can still be assigned to the target album.
    d->uploadedIds.append(assetId);
    ++d->done;
    d->progress->setValue(d->done);
    nextUpload();
}

void ImmichWindow::finishUpload()
{
    d->uploading = false;
    setTransferButtonsEnabled(true);
    d->cancelBtn->setVisible(false);

    if (!d->tempSidecar.isEmpty())
    {
        QFile::remove(d->tempSidecar);
        d->tempSidecar.clear();
    }

    if (d->cancelling)
    {
        d->cancelling = false;
        d->status->setText(QStringLiteral("Upload cancelled after %1 of %2 items.")
                               .arg(d->done)
                               .arg(d->total));
        return;
    }

    if (!d->targetAlbumId.isEmpty() && !d->uploadedIds.isEmpty())
    {
        d->status->setText(QStringLiteral("Adding %1 items to the album...")
                               .arg(d->uploadedIds.size()));
        d->talker->addToAlbum(d->targetAlbumId, d->uploadedIds);
        return;
    }

    d->status->setText(QStringLiteral("Uploaded %1 of %2 items (%3 already on the server, %4 failed).")
                           .arg(d->done - d->failed - d->alreadyThere)
                           .arg(d->total)
                           .arg(d->alreadyThere)
                           .arg(d->failed));
}

void ImmichWindow::slotAlbumUpdated()
{
    d->status->setText(QStringLiteral("Uploaded %1 of %2 items (%3 already on the server, %4 failed) and added them to the album.")
                           .arg(d->done - d->failed - d->alreadyThere)
                           .arg(d->total)
                           .arg(d->alreadyThere)
                           .arg(d->failed));
}

void ImmichWindow::slotDownloadAlbum()
{
    if (d->uploading || d->downloading)
    {
        return;
    }

    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());

    QListWidgetItem* const albumItem = d->albumList->currentItem();

    if (!albumItem)
    {
        d->status->setText(QStringLiteral("Select an album to download."));
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Download album to"), QDir::homePath());

    if (dir.isEmpty())
    {
        return;
    }

    d->dlDir         = dir;
    d->dlPending.clear();
    d->dlTotal       = 0;
    d->dlDone        = 0;
    d->dlDownloaded  = 0;
    d->dlSkipped     = 0;
    d->dlFailed      = 0;
    d->downloading   = true;
    d->dlListing     = true;

    setTransferButtonsEnabled(false);
    d->cancelBtn->setVisible(true);
    d->status->setText(QStringLiteral("Listing album assets..."));

    d->talker->listAlbumAssets(albumItem->data(Qt::UserRole).toString());
}

void ImmichWindow::slotAlbumAssets(const QList<ImmichAssetRef>& assets)
{
    d->dlListing = false;
    d->dlTotal   = assets.size();

    QSet<QString> claimed;

    for (const ImmichAssetRef& asset : assets)
    {
        if (asset.fileName.isEmpty())
        {
            ++d->dlFailed;
            ++d->dlDone;
            continue;
        }

        QString name = asset.fileName;

        if (!claimed.contains(name) && QFileInfo::exists(d->dlDir + QLatin1Char('/') + name))
        {
            // Already downloaded by an earlier run.
            claimed.insert(name);
            ++d->dlSkipped;
            ++d->dlDone;
            continue;
        }

        if (claimed.contains(name))
        {
            // A different asset in the same album carries the same
            // file name: keep both, disambiguated by asset id.
            const QFileInfo info(name);
            const QString suffix = info.suffix().isEmpty() ? QString()
                                                            : QStringLiteral(".") + info.suffix();
            name = info.completeBaseName()
                   + QStringLiteral("-") + asset.id.left(8)
                   + suffix;

            if (QFileInfo::exists(d->dlDir + QLatin1Char('/') + name))
            {
                claimed.insert(name);
                ++d->dlSkipped;
                ++d->dlDone;
                continue;
            }
        }

        claimed.insert(name);
        ImmichAssetRef target = asset;
        target.fileName       = name;
        d->dlPending.append(target);
    }

    d->progress->setRange(0, d->dlTotal);
    d->progress->setValue(d->dlDone);
    d->progress->setVisible(true);

    if (d->dlPending.isEmpty())
    {
        finishDownload();
        return;
    }

    nextDownload();
}

void ImmichWindow::nextDownload()
{
    const ImmichAssetRef asset = d->dlPending.takeFirst();

    d->status->setText(QStringLiteral("Downloading %1 (%2 of %3)")
                           .arg(asset.fileName)
                           .arg(d->dlDone + 1)
                           .arg(d->dlTotal));

    d->talker->downloadAsset(asset.id, d->dlDir + QLatin1Char('/') + asset.fileName);
}

void ImmichWindow::slotDownloadFinished(const QString& /*assetId*/, bool ok)
{
    if (d->cancelling)
    {
        finishDownload();
        return;
    }

    if (!d->downloading)
    {
        return;
    }

    if (ok)
    {
        ++d->dlDownloaded;
    }
    else
    {
        ++d->dlFailed;
    }

    ++d->dlDone;
    d->progress->setValue(d->dlDone);

    if (d->dlPending.isEmpty())
    {
        finishDownload();
        return;
    }

    nextDownload();
}

void ImmichWindow::finishDownload()
{
    d->downloading = false;
    setTransferButtonsEnabled(true);
    d->cancelBtn->setVisible(false);

    if (d->cancelling)
    {
        d->cancelling = false;
        d->status->setText(QStringLiteral("Download cancelled after %1 of %2 assets.")
                               .arg(d->dlDone)
                               .arg(d->dlTotal));
        return;
    }

    d->status->setText(QStringLiteral("Downloaded %1 assets (%2 already on disk, %3 failed) to %4")
                           .arg(d->dlDownloaded)
                           .arg(d->dlSkipped)
                           .arg(d->dlFailed)
                           .arg(d->dlDir));
}

void ImmichWindow::slotError(const QString& msg)
{
    if (d->cancelling)
    {
        // The aborted reply lands here: finalize the transfer
        // instead of advancing or falling back.
        d->checking = false;

        if (d->uploading)
        {
            finishUpload();
        }
        else if (d->downloading)
        {
            d->dlListing = false;
            finishDownload();
        }

        return;
    }

    if (d->checking)
    {
        // The duplicate check is an optimisation; if it fails,
        // upload everything the original way.
        d->checking = false;
        d->status->setText(QStringLiteral("Duplicate check failed (%1); uploading everything.").arg(msg));
        nextUpload();
        return;
    }

    if (d->dlListing)
    {
        d->dlListing   = false;
        d->downloading = false;
        setTransferButtonsEnabled(true);
        d->status->setText(msg);
        return;
    }

    if (d->uploading)
    {
        // One bad file must not stall the batch.
        ++d->failed;
        ++d->done;
        d->progress->setValue(d->done);
        d->status->setText(QStringLiteral("Failed: %1").arg(msg));
        nextUpload();
        return;
    }

    d->status->setText(msg);
}

void ImmichWindow::slotBusy(bool val)
{
    if (!d->uploading && !d->downloading)
    {
        d->refreshBtn->setEnabled(!val);
    }
}

QString ImmichWindow::sidecarFor(const QUrl& url, const QString& filePath)
{
    const QFileInfo info(filePath);

    // 1. An on-disk sidecar digiKam maintains next to the file is
    //    the richest source; it wins outright.
    const QString candidates[] = {
        filePath + QStringLiteral(".xmp"),
        info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".xmp")
    };

    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return candidate;
        }
    }

    const QString tempPath = QDir::tempPath()
                             + QStringLiteral("/immich-digikam-")
                             + info.fileName()
                             + QStringLiteral(".xmp");

    // 2. Metadata that lives in digiKam's database (tags, rating,
    //    caption, GPS set in digiKam but never written into the
    //    file): generate the sidecar from the item info, in the
    //    packet shape the bridge contract verified against a live
    //    server.
    if (d->iface)
    {
        const DItemInfo item(d->iface->itemInfo(url));
        const QStringList tags    = item.tagsPath();
        const int         rating  = item.rating();
        const QString     comment = item.comment();
        const bool        hasGps  = item.hasGeolocationInfo();

        if (!tags.isEmpty() || rating > 0 || !comment.isEmpty() || hasGps)
        {
            QString elems;

            if (!comment.isEmpty())
            {
                elems += QStringLiteral("\n   <dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">%1</rdf:li></rdf:Alt></dc:description>")
                             .arg(comment.toHtmlEscaped());
            }

            if (rating > 0)
            {
                elems += QStringLiteral("\n   <xmp:Rating>%1</xmp:Rating>").arg(rating);
            }

            if (!tags.isEmpty())
            {
                elems += QStringLiteral("\n   <digiKam:TagsList><rdf:Bag>");

                for (const QString& tag : tags)
                {
                    elems += QStringLiteral("\n      <rdf:li>%1</rdf:li>").arg(tag.toHtmlEscaped());
                }

                elems += QStringLiteral("\n   </rdf:Bag></digiKam:TagsList>");
            }

            if (hasGps)
            {
                auto gpsCoord = [](double value, bool isLatitude)
                {
                    const double absolute = std::fabs(value);
                    const int    degrees  = int(absolute);
                    const double minutes  = (absolute - degrees) * 60.0;
                    const QChar  hemi     = isLatitude ? (value >= 0 ? QLatin1Char('N') : QLatin1Char('S'))
                                                       : (value >= 0 ? QLatin1Char('E') : QLatin1Char('W'));
                    return QStringLiteral("%1,%2%3").arg(degrees).arg(minutes, 0, 'f', 4).arg(hemi);
                };

                elems += QStringLiteral("\n   <exif:GPSLatitude>%1</exif:GPSLatitude>").arg(gpsCoord(item.latitude(), true));
                elems += QStringLiteral("\n   <exif:GPSLongitude>%1</exif:GPSLongitude>").arg(gpsCoord(item.longitude(), false));
            }

            const QString xmp = QStringLiteral(
                "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
                " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
                "  <rdf:Description rdf:about=\"\"\n"
                "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
                "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
                "    xmlns:exif=\"http://ns.adobe.com/exif/1.0/\"\n"
                "    xmlns:digiKam=\"http://www.digikam.org/ns/1.0/\">%1\n"
                "  </rdf:Description>\n"
                " </rdf:RDF>\n"
                "</x:xmpmeta>\n").arg(elems);

            QFile file(tempPath);

            if (file.open(QIODevice::WriteOnly) && file.write(xmp.toUtf8()) == xmp.toUtf8().size())
            {
                d->tempSidecar = tempPath;
                return tempPath;
            }
        }
    }

    // 3. Fall back to the file's embedded XMP packet, written out
    //    as a sidecar so Immich gives it sidecar precedence.
    DMetadata meta;

    if (meta.load(filePath))
    {
        const QByteArray xmp = meta.getXmp();

        if (!xmp.isEmpty())
        {
            QFile file(tempPath);

            if (file.open(QIODevice::WriteOnly) && file.write(xmp) == xmp.size())
            {
                d->tempSidecar = tempPath;
                return tempPath;
            }
        }
    }

    return QString();
}

} // namespace DigikamGenericImmichPlugin
