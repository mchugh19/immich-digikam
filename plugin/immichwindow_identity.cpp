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
#include "thumbnailinfo.h"
#include "thumbnailloadthread.h"

namespace DigikamGenericImmichPlugin
{

void ImmichWindow::markReplace(const QString& path)
{
    d->replaceOldByPath.insert(path, d->identityCandidateId);
    d->replaceStackByPath.insert(path, d->identityStackId);
}

void ImmichWindow::nextIdentity()
{
    if (d->cancelling || !d->uploading)
    {
        return;
    }

    if (d->identityQueue.isEmpty())
    {
        nextUpload();
        return;
    }

    d->identityUrl = d->identityQueue.takeFirst();
    const QString path     = d->identityUrl.toLocalFile();
    const QString fileName = QFileInfo(path).fileName();
    d->status->setText(i18n("Checking for an existing copy of %1...")
                           .arg(fileName));

    // Pass 1 scopes the search to the album being exported to:
    // within an album, the file name is the primary key. Only a
    // complete miss there earns a library-wide second pass.
    d->identityPass     = 1;
    d->identitySearching = true;
    d->talker->searchByFileName(fileName, d->targetAlbumId);
}

void ImmichWindow::slotIdentitySearch(const QString& /*name*/, const QJsonArray& items)
{
    d->identitySearching = false;

    if (d->cancelling || !d->uploading)
    {
        return;
    }

    const QUrl    url  = d->identityUrl;
    const QString path = url.toLocalFile();
    const QString fileName = QFileInfo(path).fileName();
    QList<IdentityCandidate> candidates;

    for (const QJsonValue& v : items)
    {
        const QJsonObject o = v.toObject();
        IdentityCandidate c;
        c.id            = o.value(QStringLiteral("id")).toString();
        c.name          = o.value(QStringLiteral("originalFileName")).toString();
        c.localDateTime = o.value(QStringLiteral("localDateTime")).toString();
        c.createdAt     = o.value(QStringLiteral("createdAt")).toString();
        c.stackId       = o.value(QStringLiteral("stack")).toObject()
                           .value(QStringLiteral("id")).toString();
        c.width         = o.value(QStringLiteral("width")).toInt();
        c.height        = o.value(QStringLiteral("height")).toInt();
        candidates.append(c);
    }

    QString dateStr;
    int     width  = 0;
    int     height = 0;

    if (d->iface)
    {
        const DItemInfo item(d->iface->itemInfo(url));
        dateStr = item.dateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        width   = item.dimensions().width();
        height  = item.dimensions().height();
    }

    const IdentityMatch match = chooseIdentityMatch(candidates, fileName,
                                                    dateStr, width, height);

    if (match.assetId.isEmpty())
    {
        if (d->identityPass == 1 && !d->targetAlbumId.isEmpty() && !match.nameSeen)
        {
            // The target album holds nothing with this name at
            // all: the photo may live in another album (moved
            // in Immich, say), so try once library-wide before
            // treating the file as new.
            d->identityPass = 2;
            d->identitySearching = true;
            d->talker->searchByFileName(fileName);
            return;
        }

        d->pending.append(url);
        nextIdentity();
        return;
    }

    d->identityCandidateId  = match.assetId;
    d->identityStackId      = match.stackId;
    d->identityStrict       = match.strictTime;
    d->identityDimsDiffer   = match.dimsDiffer;
    d->identityWaitingThumb = true;
    d->talker->fetchThumbnail(match.assetId);
}

void ImmichWindow::slotIdentityThumb(const QString& /*assetId*/, const QByteArray& bytes)
{
    if (d->cancelling || !d->uploading)
    {
        return;
    }

    const QString path = d->identityUrl.toLocalFile();

    // The local frame for the comparison comes from digiKam's
    // thumbnail machinery, like the selection previews: it
    // applies the EXIF orientation (matching the server
    // thumbnail's) and decodes the formats digiKam supports
    // beyond Qt's image readers. 256 is the largest thumbnail
    // digiKam serves; the exact size is immaterial here, the
    // comparison helpers rescale both frames internally. A
    // cache hit continues at once, a miss continues in
    // slotLocalThumbnailLoaded when the load lands.
    d->identityThumbBytes   = bytes;
    d->identityWaitingLocal = true;

    QPixmap pix;

    if (d->localThumbThread->find(ThumbnailIdentifier(path), pix, 256))
    {
        d->identityWaitingLocal = false;
        d->identityThumbBytes.clear();
        finishIdentityCheck(pix.toImage(), bytes);
    }
}

void ImmichWindow::finishIdentityCheck(const QImage& localImage, const QByteArray& bytes)
{
    if (d->cancelling || !d->uploading)
    {
        return;
    }

    const QUrl    url  = d->identityUrl;
    const QString path = url.toLocalFile();

    QImage remoteImage;
    remoteImage.loadFromData(bytes);

    if (d->identityDimsDiffer)
    {
        // Cropped or resized since the upload. That is a real
        // edit by definition; the only question is whether this
        // really is the same photo, which the containment check
        // answers. Proven: replace. Unproven: upload as new.
        if (!localImage.isNull() && !remoteImage.isNull()
            && imageContains(remoteImage, localImage))
        {
            markReplace(path);
        }

        d->pending.append(url);
        nextIdentity();
        return;
    }

    // Undecodable on either side (RAW files, say) counts as
    // "same": sync the metadata, never destroy on uncertainty.
    const bool same = localImage.isNull() || remoteImage.isNull()
                      || imagesLookSame(localImage, remoteImage);

    if (same)
    {
        // The same photo with changed bytes: keep the server's
        // copy, sync digiKam's metadata onto it. An asset an
        // earlier file in this selection already claimed counts
        // as a duplicate within the selection, not as another
        // asset found.
        if (claimAssetId(d->identityCandidateId))
        {
            ++d->alreadyThere;
        }

        queueSyncJob(d->identityCandidateId, path, dkFieldsFor(url));
        ++d->done;
        d->progress->setValue(d->done);
        nextIdentity();
        return;
    }

    if (d->identityStrict)
    {
        // A real edit of a photo the server holds: upload it and
        // replace the old copy once the upload lands.
        markReplace(path);
    }

    d->pending.append(url);
    nextIdentity();
}

} // namespace DigikamGenericImmichPlugin
