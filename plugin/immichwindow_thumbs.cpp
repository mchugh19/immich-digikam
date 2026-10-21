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

#include "immichwindow.h"
#include "immichwindow_p.h"

// Qt includes

#include <algorithm>
#include <cmath>

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
#include "loadingdescription.h"
#include "thumbnailloadthread.h"
#include "thumbnailinfo.h"

namespace DigikamGenericImmichPlugin
{

void ImmichWindow::fillUploadPreview()
{
    d->previewList->clear();

    if (!d->iface)
    {
        return;
    }

    QPixmap blank(96, 96);
    blank.fill(Qt::transparent);
    const QIcon placeholder(blank);

    const QList<QUrl> selected = d->iface->currentSelectedItems();

    for (const QUrl& url : selected)
    {
        const QString path = url.toLocalFile();
        auto* const item = new QListWidgetItem(QFileInfo(path).fileName(), d->previewList);
        item->setSizeHint(QSize(116, 128));
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);

        // The selection holds local files, so their thumbnails
        // come from digiKam's thumbnail machinery: it decodes
        // every format digiKam supports (HEIF included), where
        // QImageReader has no reader for some of them. A cache
        // hit fills the icon at once; a miss starts the load and
        // slotLocalThumbnailLoaded fills the icon on arrival.
        QPixmap pix;

        if (d->localThumbThread->find(ThumbnailIdentifier(path), pix, 96)
            && !pix.isNull())
        {
            if (pix.width() > 96 || pix.height() > 96)
            {
                pix = pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            item->setIcon(QIcon(pix));
        }
        else
        {
            item->setIcon(placeholder);
        }
    }
}

void ImmichWindow::slotLocalThumbnailLoaded(const LoadingDescription& desc,
                                            const QPixmap& pix)
{
    const QString path = desc.filePath;

    // The identity check may be waiting for this file's frame:
    // its comparison continues with whatever arrived, a null
    // pixmap included (undecodable counts as "same" there).
    if (d->identityWaitingLocal && path == d->identityUrl.toLocalFile())
    {
        d->identityWaitingLocal = false;
        const QByteArray bytes = d->identityThumbBytes;
        d->identityThumbBytes.clear();
        finishIdentityCheck(pix.toImage(), bytes);
    }

    if (pix.isNull())
    {
        return;
    }

    QPixmap scaled = pix;

    if (scaled.width() > 96 || scaled.height() > 96)
    {
        scaled = scaled.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    for (int i = 0 ; i < d->previewList->count() ; ++i)
    {
        QListWidgetItem* const item = d->previewList->item(i);

        if (item->data(Qt::UserRole).toString() == path)
        {
            item->setIcon(QIcon(scaled));
        }
    }
}

void ImmichWindow::queueThumb(bool cover, const QString& key, const QString& assetId)
{
    if (assetId.isEmpty())
    {
        return;
    }

    Private::ThumbJob job;
    job.cover   = cover;
    job.key     = key;
    job.assetId = assetId;
    d->thumbJobs.append(job);
    pumpThumbs();
}

void ImmichWindow::pumpThumbs()
{
    while (d->thumbInFlight.size() < Private::maxThumbFetches && !d->thumbJobs.isEmpty())
    {
        const Private::ThumbJob job = d->thumbJobs.takeFirst();

        if (d->thumbInFlight.contains(job.assetId))
        {
            // A fetch for this asset is already running; park the
            // job with it and let its reply satisfy both.
            d->thumbInFlight[job.assetId].append(job);
            continue;
        }

        d->thumbInFlight.insert(job.assetId, { job });

        // Disk cache first: a hit is delivered through the same
        // completion path as a network reply (which re-enters
        // pumpThumbs; the loop just re-checks its conditions).
        QFile cached(d->thumbCacheDir + QLatin1Char('/')
                     + job.assetId + QStringLiteral(".thumb"));

        if (cached.open(QIODevice::ReadOnly))
        {
            const QByteArray bytes = cached.readAll();
            cached.close();

            if (!bytes.isEmpty())
            {
                slotThumbnail(job.assetId, bytes);
                continue;
            }
        }

        d->talker->fetchThumbnail(job.assetId);
    }
}

void ImmichWindow::dropThumbJobs(bool covers)
{
    for (int i = d->thumbJobs.size() - 1 ; i >= 0 ; --i)
    {
        if (d->thumbJobs.at(i).cover == covers)
        {
            d->thumbJobs.removeAt(i);
        }
    }

    // Parked jobs go too. A fetch already in flight cannot be
    // singled out for abort; its reply still lands in the caches,
    // and the UI application checks the item still exists.
    for (auto it = d->thumbInFlight.begin() ; it != d->thumbInFlight.end() ;)
    {
        QList<Private::ThumbJob>& jobs = it.value();

        for (int i = jobs.size() - 1 ; i >= 0 ; --i)
        {
            if (jobs.at(i).cover == covers)
            {
                jobs.removeAt(i);
            }
        }

        if (jobs.isEmpty())
        {
            it = d->thumbInFlight.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ImmichWindow::startPreview(const QString& sourceId)
{
    if (d->uploading || d->downloading || sourceId.isEmpty())
    {
        return;
    }

    const QString key = (d->sourceIsTags ? QStringLiteral("tag:")
                                         : QStringLiteral("album:")) + sourceId;

    if (d->session->hasListing(key))
    {
        d->previewSourceId = sourceId;
        d->previewAssets   = d->session->listing(key);
        fillPreview();
        return;
    }

    if (d->listingPurpose != Private::ListingPurpose::None)
    {
        return;
    }

    d->previewSourceId = sourceId;
    d->listingKey      = key;
    ++d->previewGen;
    d->previewList->clear();
    d->previewAssets.clear();
    dropThumbJobs(false);
    d->listingPurpose = Private::ListingPurpose::Preview;
    d->status->setText(i18n("Listing contents..."));

    if (d->sourceIsTags)
    {
        d->talker->listTagAssets(sourceId);
    }
    else
    {
        d->talker->listAlbumAssets(sourceId);
    }
}

void ImmichWindow::fillPreview()
{
    d->previewList->clear();
    dropThumbJobs(false);

    QList<ImmichAssetRef> sorted = d->previewAssets;

    switch (d->previewSort->currentIndex())
    {
        case 1:
            std::sort(sorted.begin(), sorted.end(),
                      [](const ImmichAssetRef& a, const ImmichAssetRef& b)
                      { return a.fileCreatedAt > b.fileCreatedAt; });
            break;

        case 2:
            std::sort(sorted.begin(), sorted.end(),
                      [](const ImmichAssetRef& a, const ImmichAssetRef& b)
                      { return QString::compare(a.fileName, b.fileName, Qt::CaseInsensitive) < 0; });
            break;

        default:
            std::sort(sorted.begin(), sorted.end(),
                      [](const ImmichAssetRef& a, const ImmichAssetRef& b)
                      { return a.fileCreatedAt < b.fileCreatedAt; });
            break;
    }

    // Items start with a full-size transparent placeholder icon:
    // an item created icon-less gets a text-only geometry, and
    // the thumbnail arriving later is then painted squashed into
    // that short box instead of the 96x96 the view advertises.
    QPixmap blank(96, 96);
    blank.fill(Qt::transparent);
    const QIcon placeholder(blank);

    for (const ImmichAssetRef& asset : sorted)
    {
        auto* const item = new QListWidgetItem(asset.fileName, d->previewList);
        item->setData(Qt::UserRole, asset.id);
        item->setToolTip(asset.fileCreatedAt.left(10));
        item->setSizeHint(QSize(116, 128));

        const auto cached = d->thumbCache.constFind(asset.id);

        if (cached != d->thumbCache.constEnd())
        {
            item->setIcon(QIcon(*cached));
        }
        else
        {
            item->setIcon(placeholder);
            queueThumb(false, asset.id, asset.id);
        }
    }

    applyFilter();
    d->status->setText(i18n("%1 items").arg(sorted.size()));
}

void ImmichWindow::slotPreviewSortChanged()
{
    if (!d->previewAssets.isEmpty())
    {
        fillPreview();
    }
}

void ImmichWindow::slotThumbnail(const QString& assetId, const QByteArray& bytes)
{
    if (d->identityWaitingThumb && assetId == d->identityCandidateId)
    {
        d->identityWaitingThumb = false;
        slotIdentityThumb(assetId, bytes);
        return;
    }

    const QList<Private::ThumbJob> jobs = d->thumbInFlight.take(assetId);

    if (!bytes.isEmpty())
    {
        QPixmap pix;

        if (pix.loadFromData(bytes))
        {
            // Persist for the next session (rewriting a cache hit
            // just refreshes its mtime for the size prune).
            QFile out(d->thumbCacheDir + QLatin1Char('/') + assetId
                      + QStringLiteral(".thumb"));

            if (out.open(QIODevice::WriteOnly))
            {
                out.write(bytes);
            }

            d->thumbCache.insert(assetId, pix);

            for (const Private::ThumbJob& job : jobs)
            {
                if (job.cover)
                {
                    d->coverCache.insert(job.key, pix);

                    if (QListWidgetItem* const item = findAlbumItem(job.key))
                    {
                        setAlbumCover(item, pix);
                    }
                }
                else
                {
                    for (int i = 0 ; i < d->previewList->count() ; ++i)
                    {
                        QListWidgetItem* const item = d->previewList->item(i);

                        if (item->data(Qt::UserRole).toString() == assetId)
                        {
                            item->setIcon(QIcon(pix));
                            break;
                        }
                    }
                }
            }
        }
    }

    pumpThumbs();
}

} // namespace DigikamGenericImmichPlugin
