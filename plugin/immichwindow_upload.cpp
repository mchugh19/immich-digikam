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
#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QComboBox>
#include <QDateTime>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QImageReader>
#include <QPixmap>
#include <QShowEvent>
#include <QRadioButton>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

// KDE includes

#include <KFile>
#include <KUrlRequester>
#include <klocalizedstring.h>

// Local includes

#include "dinfointerface.h"
#include "dmetadata.h"
#include "immichsession.h"
#include "immichsync.h"
#include "immichtalker.h"
#include "immichxmp.h"

namespace DigikamGenericImmichPlugin
{

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
        d->status->setText(i18n("No items selected in digiKam."));
        return;
    }

    d->targetAlbumId = currentSourceId();

    d->pending     = items;
    d->uploadedIds.clear();
    d->total       = items.size();
    d->done        = 0;
    d->failed      = 0;
    d->alreadyThere = 0;
    d->uploading   = true;

    d->syncJobs.clear();
    d->syncTagAdds.clear();
    d->syncCtx.clear();
    d->syncNextIndex = 0;
    d->syncFetchInFlight = 0;
    d->syncPatchInFlight = 0;
    d->syncProcessed = 0;
    d->syncUpdated   = 0;
    d->syncClean     = 0;
    d->syncFailed    = 0;
    d->syncConflicts = 0;
    d->syncRan       = false;
    d->syncPermsDenied = false;
    d->syncAborted   = false;
    d->albumAssigned = false;

    d->identityQueue.clear();
    d->identityCandidateId.clear();
    d->identityStackId.clear();
    d->identityStrict = false;
    d->identityDimsDiffer = false;
    d->identityWaitingThumb = false;
    d->identityPass = 1;
    d->identityNameSeen = false;
    d->matchedSync   = 0;
    d->matchedMulti  = 0;
    d->stackedCount  = 0;
    d->trashFallbackCount = 0;
    d->overwriteCount = 0;
    d->overwriteQueue.clear();
    d->replaceByStack = (d->session->replaceMode() != QStringLiteral("trash"));
    d->replaceOldByPath.clear();
    d->replaceStackByPath.clear();
    d->replaces.clear();
    d->restoreQueue.clear();
    d->restoreAlbums.clear();
    d->restoreTrashQueue.clear();
    d->restoring = false;

    setTransferButtonsEnabled(false);
    d->progress->setRange(0, d->total);
    d->progress->setValue(0);
    d->cancelBtn->setEnabled(true);

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
        d->status->setText(i18n("Checking which files are already on the server..."));
        d->talker->checkUploads(checksums);
        return;
    }

    nextUpload();
}

void ImmichWindow::slotUploadCheck(const QMap<QString, QString>& duplicatesByPath)
{
    d->checking = false;

    QList<QUrl> remaining;

    for (const QUrl& url : std::as_const(d->pending))
    {
        const QString assetId = duplicatesByPath.value(url.toLocalFile());

        if (assetId.isEmpty())
        {
            remaining.append(url);
        }
        else
        {
            // Already on the server: count it, and keep its id so
            // a target album still gets the asset assigned. Its
            // digiKam metadata is queued for the diff sync that
            // runs after the uploads: the file itself is not
            // re-sent, but changed tags, rating, caption and GPS
            // still reach the server through the API.
            d->uploadedIds.append(assetId);
            Private::SyncJob job;
            job.assetId   = assetId;
            job.localPath = url.toLocalFile();
            job.dk        = dkFieldsFor(url);
            d->syncJobs.append(job);
            ++d->alreadyThere;
            ++d->done;
        }
    }

    // What the checksum check missed goes through identity
    // matching first: a file whose bytes changed in digiKam may
    // still be a photo the server already has.
    d->pending       = QList<QUrl>();
    d->identityQueue = remaining;
    d->progress->setValue(d->done);
    nextIdentity();
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

    d->status->setText(i18n("Uploading %1 (%2 of %3)")
                           .arg(info.fileName())
                           .arg(d->done + 1)
                           .arg(d->total));

    d->currentUploadFields = dkFieldsFor(url);
    d->haveUploadFields    = true;
    d->currentUploadPath   = path;
    d->talker->uploadAsset(path, sidecarFor(url, path));
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

    if (d->haveUploadFields && !assetId.isEmpty())
    {
        // Baseline snapshot for a fresh upload: what digiKam
        // sent is what the server now holds (the sidecar merge
        // may add preserved fields, but the synced fields are
        // these). The next run diffs against this.
        storeSnapshot(assetId, d->currentUploadFields, QString(), QString(),
                      d->currentUploadFields);
        d->haveUploadFields = false;
    }

    const QString replacedOld = d->replaceOldByPath.take(d->currentUploadPath);

    if (!replacedOld.isEmpty() && !assetId.isEmpty())
    {
        // This upload replaces an edited photo's old copy: its
        // albums are restored and the old copy trashed after the
        // uploads finish, and the new copy joins the sync set so
        // its metadata converges too.
        Private::ReplaceCtx ctx;
        ctx.oldId     = replacedOld;
        ctx.newId     = assetId;
        ctx.localPath = d->currentUploadPath;
        ctx.stackId   = d->replaceStackByPath.take(d->currentUploadPath);
        d->replaces.append(ctx);

        Private::SyncJob job;
        job.assetId   = assetId;
        job.localPath = d->currentUploadPath;
        job.dk        = d->currentUploadFields;
        d->syncJobs.append(job);
    }

    ++d->done;
    d->progress->setValue(d->done);
    nextUpload();
}

void ImmichWindow::finishUpload()
{
    d->uploading = false;
    setTransferButtonsEnabled(true);
    d->cancelBtn->setEnabled(false);


    if (!d->tempSidecar.isEmpty())
    {
        QFile::remove(d->tempSidecar);
        d->tempSidecar.clear();
    }

    if (d->cancelling)
    {
        d->cancelling = false;
        d->status->setText(i18n("Upload cancelled after %1 of %2 items.")
                               .arg(d->done)
                               .arg(d->total));
        return;
    }

    if (!d->targetAlbumId.isEmpty() && !d->uploadedIds.isEmpty())
    {
        d->status->setText(i18n("Adding %1 items to the album...")
                               .arg(d->uploadedIds.size()));
        d->talker->addToAlbum(d->targetAlbumId, d->uploadedIds);
        return;
    }

    startRestoreOrSync();
}

void ImmichWindow::slotAlbumUpdated(bool changed)
{
    if (d->restoring)
    {
        restoreAdvanceAdd();
        return;
    }

    if (changed && !d->targetAlbumId.isEmpty())
    {
        // The album actually gained assets; its cached contents
        // listing is stale. (When every asset was already in the
        // album, the server changed nothing and the cache stays.)
        d->session->invalidateListing(QStringLiteral("album:") + d->targetAlbumId);
    }

    d->albumAssigned = true;
    startRestoreOrSync();
}

void ImmichWindow::printUploadSummary()
{
    // One line, outcome counts only. The mechanisms that used
    // to append their own sentences here (identity matching,
    // the metadata sync, the replace handling) overlap in their
    // counts, so reporting each separately produced a paragraph
    // that contradicted itself; here every outcome is counted
    // once, in the order it happened.
    const int uploaded = qMax(0, d->done - d->failed - d->alreadyThere);

    QString uploadedPart = i18n("%1 uploaded").arg(uploaded);

    if (d->stackedCount > 0 || d->overwriteCount > 0 || d->trashFallbackCount > 0)
    {
        QStringList details;

        if (d->stackedCount > 0)
        {
            details << i18n("%1 stacked").arg(d->stackedCount);
        }

        if (d->overwriteCount > 0)
        {
            details << i18n("%1 overwritten").arg(d->overwriteCount);
        }

        if (d->trashFallbackCount > 0)
        {
            details << i18np("%1 could not be stacked, old copy in trash",
                             "%1 could not be stacked, old copies in trash",
                             d->trashFallbackCount);
        }

        uploadedPart += QStringLiteral(" (") + details.join(QStringLiteral(", "))
                        + QStringLiteral(")");
    }

    QStringList parts;
    parts << uploadedPart;
    parts << i18n("%1 already in Immich").arg(d->alreadyThere);

    if (d->syncRan)
    {
        if (d->syncUpdated > 0 && d->syncClean > 0)
        {
            parts << i18n("%1 metadata updated, %2 unchanged")
                         .arg(d->syncUpdated)
                         .arg(d->syncClean);
        }
        else if (d->syncUpdated > 0)
        {
            parts << i18n("%1 metadata updated").arg(d->syncUpdated);
        }
        else if (d->syncClean > 0 && !d->syncPermsDenied)
        {
            parts << i18n("metadata unchanged");
        }

        if (d->syncConflicts > 0)
        {
            parts << i18np("%1 conflict settled by timestamp",
                           "%1 conflicts settled by timestamp",
                           d->syncConflicts);
        }

        if (d->syncPermsDenied)
        {
            parts << i18n("metadata sync refused: the API key needs"
                          " asset.update, tag.create and tag.asset");
        }
    }

    if (d->albumAssigned)
    {
        parts << i18n("album updated");
    }

    // A permission refusal already explains its failures; do
    // not count them twice.
    const int failedTotal = d->failed + (d->syncPermsDenied ? 0 : d->syncFailed);

    if (failedTotal > 0)
    {
        parts << i18np("%1 failure", "%1 failures", failedTotal);
    }
    else
    {
        parts << i18n("no failures");
    }

    const QString prefix = i18np("%1 item", "%1 items", d->total);
    d->status->setText(prefix + QStringLiteral(": ")
                       + parts.join(QStringLiteral(", ")) + QStringLiteral("."));
}

SyncFields ImmichWindow::dkFieldsFor(const QUrl& url) const
{
    SyncFields fields;

    if (!d->iface)
    {
        return fields;
    }

    const DItemInfo item(d->iface->itemInfo(url));
    fields.tags        = item.tagsPath();
    fields.rating      = (item.rating() >= 1 && item.rating() <= 5) ? item.rating() : 0;
    fields.description = item.comment();

    if (item.hasGeolocationInfo())
    {
        fields.hasGps    = true;
        fields.latitude  = item.latitude();
        fields.longitude = item.longitude();
    }

    return fields;
}

QString ImmichWindow::sidecarFor(const QUrl& url, const QString& filePath)
{
    const QFileInfo info(filePath);

    const QString tempPath = QDir::tempPath()
                             + QStringLiteral("/immich-digikam-")
                             + info.fileName()
                             + QStringLiteral(".xmp");

    // The base packet: whatever XMP the file already carries,
    //    from its on-disk sidecar if it has one, else its embedded
    //    packet. It is the preservation source below.
    QString   basePacket;
    QString   onDiskSidecar;
    const QString candidates[] = {
        filePath + QStringLiteral(".xmp"),
        info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".xmp")
    };

    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            onDiskSidecar = candidate;
            break;
        }
    }

    if (!onDiskSidecar.isEmpty())
    {
        QFile base(onDiskSidecar);

        if (base.open(QIODevice::ReadOnly))
        {
            basePacket = QString::fromUtf8(base.readAll());
        }
    }
    else
    {
        DMetadata meta;

        if (meta.load(filePath))
        {
            basePacket = QString::fromUtf8(meta.getXmp());
        }
    }

    // 1. digiKam's database is the user's current intent: this
    //    tool runs inside digiKam on digiKam's selection, and an
    //    export should carry the tags and rating as they stand in
    //    digiKam now. Generate the sidecar from the item info, in
    //    the packet shape the bridge contract verified against a
    //    live server, merged over the base packet so any other
    //    XMP the file carries (digiKam's label properties, say)
    //    is preserved rather than dropped. (An on-disk sidecar
    //    used to win outright; a stale fixture-era .xmp next to
    //    a test photo then silently overrode fresh digiKam edits,
    //    exporting a rating and tags the user had changed.)
    if (d->iface)
    {
        const DItemInfo item(d->iface->itemInfo(url));
        const QStringList tags    = item.tagsPath();
        // digiKam reports an unset rating as -1; passing that
        // through would write xmp:Rating -1, which Immich reads
        // as "rejected". Only a real 1..5 rating is a rating.
        const int         rating  = (item.rating() >= 1 && item.rating() <= 5)
                                        ? item.rating() : 0;
        const QString     comment = item.comment();
        const bool        hasGps  = item.hasGeolocationInfo();

        if (!tags.isEmpty() || rating > 0 || !comment.isEmpty() || hasGps)
        {
            QStringList generatedKeys;

            if (!tags.isEmpty())
            {
                generatedKeys << QStringLiteral("digiKam:TagsList");
            }

            if (rating > 0)
            {
                generatedKeys << QStringLiteral("xmp:Rating");
            }

            if (!comment.isEmpty())
            {
                generatedKeys << QStringLiteral("dc:description");
            }

            if (hasGps)
            {
                generatedKeys << QStringLiteral("exif:GPSLatitude")
                              << QStringLiteral("exif:GPSLongitude");
            }

            const QString xmp = mergeSidecarPackets(
                buildSidecarPacket(tags, rating, comment,
                                   hasGps,
                                   hasGps ? item.latitude() : 0.0,
                                   hasGps ? item.longitude() : 0.0),
                basePacket, generatedKeys);

            QFile file(tempPath);

            if (file.open(QIODevice::WriteOnly) && file.write(xmp.toUtf8()) == xmp.toUtf8().size())
            {
                d->tempSidecar = tempPath;
                return tempPath;
            }
        }
    }

    // 2. digiKam's database held nothing for the item: the
    //    file's own sidecar, if it has one, travels as it stands.
    if (!onDiskSidecar.isEmpty())
    {
        return onDiskSidecar;
    }

    // 3. Otherwise the embedded packet, written out as a sidecar
    //    so Immich gives it sidecar precedence.
    if (!basePacket.isEmpty())
    {
        QFile file(tempPath);

        if (file.open(QIODevice::WriteOnly) && file.write(basePacket.toUtf8()) == basePacket.toUtf8().size())
        {
            d->tempSidecar = tempPath;
            return tempPath;
        }
    }

    return QString();
}

} // namespace DigikamGenericImmichPlugin
