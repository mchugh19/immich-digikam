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

// Self includes

#include "immichwindow_p.h"

// Qt includes

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QComboBox>
#include <QDateTime>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPixmap>
#include <QRadioButton>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QVBoxLayout>

// KDE includes

#include <KFile>
#include <KUrlRequester>
#include <klocalizedstring.h>

// Local includes

#include "dinfointerface.h"
#include "immichsession.h"
#include "immichsync.h"
#include "immichtalker.h"
#include "immichxmp.h"

namespace DigikamGenericImmichPlugin
{

void ImmichWindow::slotDownloadAlbum()
{
    if (d->uploading || d->downloading)
    {
        return;
    }

    syncSessionFromEdits();

    const QString sourceId = currentSourceId();

    if (sourceId.isEmpty())
    {
        d->status->setText(i18n("Select an album or tag to download."));
        return;
    }

    const QString dir = d->downloadDirEdit->url().toLocalFile();

    if (dir.isEmpty())
    {
        d->status->setText(i18n("Choose a download folder first."));
        return;
    }

    d->session->setDownloadDir(dir);
    d->session->save();

    d->dlDir         = dir;
    d->dlPending.clear();
    d->metaPending.clear();
    d->metaTotal     = 0;
    d->metaDone      = 0;
    d->metaUpdated   = 0;
    d->dlTotal       = 0;
    d->dlDone        = 0;
    d->dlDownloaded  = 0;
    d->dlSkipped     = 0;
    d->dlFailed      = 0;
    d->downloading    = true;
    d->dlListing      = true;
    d->listingPurpose = Private::ListingPurpose::Download;
    d->listingKey     = listingKeyFor(sourceId);
    dropThumbJobs(false);

    setTransferButtonsEnabled(false);
    d->cancelBtn->setEnabled(true);
    d->status->setText(i18n("Listing assets..."));

    listSourceAssets(sourceId);
}

void ImmichWindow::slotAlbumAssets(const QList<ImmichAssetRef>& assets)
{
    if (d->listingPurpose == Private::ListingPurpose::Preview)
    {
        d->listingPurpose = Private::ListingPurpose::None;
        d->previewAssets  = assets;
        d->session->cacheListing(d->listingKey, assets);
        fillPreview();

        // The selection moved on while this listing was in flight:
        // preview what is selected now.
        const QString currentId = currentSourceId();

        if (!currentId.isEmpty() && currentId != d->previewSourceId)
        {
            startPreview(currentId);
        }

        return;
    }

    if (d->listingPurpose != Private::ListingPurpose::Download)
    {
        // A listing nobody is waiting for (superseded, or its
        // purpose was cleared by an error): drop it. Falling
        // through here would treat a preview result as a download
        // and start writing files.
        return;
    }

    d->listingPurpose = Private::ListingPurpose::None;
    d->session->cacheListing(d->listingKey, assets);
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
        }

        if (QFileInfo::exists(d->dlDir + QLatin1Char('/') + name))
        {
            // Already downloaded by an earlier run: skip the file,
            // but queue a metadata refresh so Immich-side tag,
            // rating or caption changes still reach digiKam.
            claimed.insert(name);
            d->metaPending.append(qMakePair(asset.id, d->dlDir + QLatin1Char('/') + name));
            continue;
        }

        claimed.insert(name);
        ImmichAssetRef target = asset;
        target.fileName       = name;
        d->dlPending.append(target);
    }

    d->metaTotal = d->metaPending.size();

    d->progress->setRange(0, d->dlTotal);
    d->progress->setValue(d->dlDone);

    continueDownloads();
}

void ImmichWindow::continueDownloads()
{
    if (!d->dlPending.isEmpty())
    {
        nextDownload();
        return;
    }

    if (!d->metaPending.isEmpty())
    {
        nextMeta();
        return;
    }

    finishDownload();
}

void ImmichWindow::nextMeta()
{
    const QPair<QString, QString> item = d->metaPending.takeFirst();

    d->status->setText(i18n("Refreshing metadata for existing files (%1 of %2)")
                           .arg(d->metaDone + 1)
                           .arg(d->metaTotal));

    d->talker->downloadSidecarOnly(item.first, item.second);
}

void ImmichWindow::slotMetaFinished(const QString& /*assetId*/, bool updated)
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

    if (updated)
    {
        ++d->metaUpdated;
    }

    ++d->dlSkipped;
    ++d->dlDone;
    ++d->metaDone;
    d->progress->setValue(d->dlDone);

    continueDownloads();
}

void ImmichWindow::nextDownload()
{
    const ImmichAssetRef asset = d->dlPending.takeFirst();

    d->status->setText(i18n("Downloading %1 (%2 of %3)")
                           .arg(asset.fileName)
                           .arg(d->dlDone + 1)
                           .arg(d->dlTotal));

    d->talker->downloadAsset(asset.id, d->dlDir + QLatin1Char('/') + asset.fileName);
}

void ImmichWindow::slotAssetRecord(const QString& assetId, const QString& targetFile,
                                   const QJsonObject& record)
{
    // One parse of the record, shared by the sidecar and the
    // snapshot: the two used to parse it separately and could
    // disagree (the sidecar's rating fallback treated an
    // explicit 0 as absent and never clamped negatives).
    const ServerFields sv = serverFieldsFromRecord(record);
    writeRecordSidecar(targetFile, sv.fields);

    // Post-pull snapshot: digiKam now holds the server's fields.
    storeSnapshot(assetId, sv.fields, sv.updatedAt, sv.fields);

    slotDownloadFinished(assetId, true);
}

void ImmichWindow::slotMetaRecord(const QString& assetId, const QString& targetFile,
                                  const QJsonObject& record)
{
    const ServerFields sv = serverFieldsFromRecord(record);
    const bool updated = writeRecordSidecar(targetFile, sv.fields);

    storeSnapshot(assetId, sv.fields, sv.updatedAt, sv.fields);

    slotMetaFinished(assetId, updated);
}

bool ImmichWindow::writeRecordSidecar(const QString& targetFile, const SyncFields& fields)
{
    // Generate a sidecar from an asset's server fields so
    // digiKam imports the tags, rating, caption and GPS
    // together with the photo. Returns false when the fields
    // carry no metadata worth writing (any local sidecar is
    // left alone).
    if (!fields.tags.isEmpty() || fields.rating != 0
        || !fields.description.isEmpty() || fields.hasGps)
    {
        const QString   packet = buildSidecarPacket(fields.tags, fields.rating,
                                                    fields.description, fields.hasGps,
                                                    fields.latitude, fields.longitude);
        const QByteArray bytes = packet.toUtf8();

        // Already current: leave the local sidecar (and its
        // timestamp) untouched, and report "not updated".
        QFile existing(targetFile + QStringLiteral(".xmp"));

        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
        {
            return false;
        }

        existing.close();
        QFile file(targetFile + QStringLiteral(".xmp"));

        if (file.open(QIODevice::WriteOnly))
        {
            return file.write(bytes) == bytes.size();
        }
    }

    return false;
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

    continueDownloads();
}

void ImmichWindow::finishDownload()
{
    d->downloading = false;
    setTransferButtonsEnabled(true);
    d->cancelBtn->setEnabled(false);

    if (d->cancelling)
    {
        d->cancelling = false;
        d->status->setText(i18n("Download cancelled after %1 of %2 assets.")
                               .arg(d->dlDone)
                               .arg(d->dlTotal));
        return;
    }

    QString summary = i18n("Downloaded %1 assets (%2 already on disk, %3 failed) to %4")
                          .arg(d->dlDownloaded)
                          .arg(d->dlSkipped)
                          .arg(d->dlFailed)
                          .arg(d->dlDir);

    if (d->metaUpdated > 0)
    {
        summary += i18n("; metadata refreshed for %1 existing files").arg(d->metaUpdated);
    }

    d->status->setText(summary);
}

} // namespace DigikamGenericImmichPlugin
