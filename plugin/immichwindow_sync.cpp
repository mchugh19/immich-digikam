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

void ImmichWindow::maybeStartSync()
{
    if (d->syncJobs.isEmpty())
    {
        printUploadSummary();
        return;
    }

    d->syncing = true;
    d->syncRan = true;
    d->syncNextIndex = 0;
    d->syncFetchInFlight = 0;
    d->syncPatchInFlight = 0;
    d->syncProcessed = 0;
    d->status->setText(i18n("Syncing metadata for %1 already-uploaded items...")
                           .arg(d->syncJobs.size()));
    pumpSyncFetches();
}

void ImmichWindow::pumpSyncFetches()
{
    while (!d->syncAborted
           && d->syncFetchInFlight < Private::maxSyncFetches
           && d->syncNextIndex < d->syncJobs.size())
    {
        const Private::SyncJob& job = d->syncJobs.at(d->syncNextIndex);
        ++d->syncNextIndex;
        ++d->syncFetchInFlight;
        d->talker->fetchAssetForSync(job.assetId);
    }
}

void ImmichWindow::slotSyncRecord(const QString& assetId, const QJsonObject& record, int httpStatus)
{
    if (!d->syncing)
    {
        return;
    }

    --d->syncFetchInFlight;

    const Private::SyncJob* job = nullptr;

    for (const Private::SyncJob& candidate : std::as_const(d->syncJobs))
    {
        if (candidate.assetId == assetId)
        {
            job = &candidate;
            break;
        }
    }

    if (!job)
    {
        pumpSyncFetches();
        checkSyncPhaseDone();
        return;
    }

    Private::SyncAssetCtx ctx;

    if (httpStatus != 200 || record.isEmpty())
    {
        ctx.failed = true;
        d->syncCtx.insert(assetId, ctx);
        finishSyncAsset(assetId);
        pumpSyncFetches();
        return;
    }

    const ServerFields sv = serverFieldsFromRecord(record);
    ctx.server = sv;
    SyncPlan plan = planSync(job->dk, sv.fields);

    // Snapshot gating: did either side move since the last sync?
    const QJsonObject snapObj = d->syncState.value(assetId).toObject();
    const SyncSnapshot snap   = SyncSnapshot::fromJson(snapObj);
    const GateResult gate     = gateSync(!snapObj.isEmpty(),
                                         snap.serverUpdatedAt, sv.updatedAt,
                                         snap.dkHash, syncFieldHash(job->dk),
                                         QDateTime::fromString(sv.updatedAt, Qt::ISODateWithMs),
                                         QFileInfo(job->localPath).lastModified());

    if (gate.action == GateAction::LeaveAlone)
    {
        // Only Immich moved: leave its state exactly as it is
        // and refresh the snapshot to match.
        ++d->syncClean;
        ++d->syncProcessed;
        storeSnapshot(assetId, sv.fields, sv.updatedAt, sv.checksum, job->dk);
        pumpSyncFetches();
        checkSyncPhaseDone();
        return;
    }

    if (gate.conflict)
    {
        ++d->syncConflicts;

        if (gate.action == GateAction::DropScalars)
        {
            plan.dropScalars();
        }
    }

    ctx.plan = plan;

    for (const QString& tag : std::as_const(plan.tagsToAdd))
    {
        d->syncTagAdds[tag].append(assetId);
        ctx.wrote = true;
    }

    d->syncCtx.insert(assetId, ctx);

    if (!plan.scalars() || d->syncAborted)
    {
        finishSyncAsset(assetId);
        pumpSyncFetches();
        return;
    }

    QJsonObject body;

    if (plan.setRating)
    {
        body.insert(QStringLiteral("rating"),
                    plan.rating == 0 ? QJsonValue() : QJsonValue(plan.rating));
    }

    if (plan.setDescription)
    {
        body.insert(QStringLiteral("description"), plan.description);
    }

    if (plan.setGps)
    {
        body.insert(QStringLiteral("latitude"),  plan.latitude);
        body.insert(QStringLiteral("longitude"), plan.longitude);
    }

    ++d->syncPatchInFlight;
    d->talker->updateAsset(assetId, body);
    pumpSyncFetches();
}

void ImmichWindow::slotAssetPatched(const QString& assetId, int httpStatus)
{
    if (!d->syncing)
    {
        return;
    }

    --d->syncPatchInFlight;

    auto it = d->syncCtx.find(assetId);

    if (it == d->syncCtx.end())
    {
        checkSyncPhaseDone();
        return;
    }

    if (httpStatus == 403)
    {
        d->syncPermsDenied = true;
        d->syncAborted     = true;
        it->failed         = true;
    }
    else if (httpStatus < 200 || httpStatus >= 300)
    {
        it->failed = true;
    }
    else
    {
        it->wrote = true;
    }

    finishSyncAsset(assetId);
}

void ImmichWindow::finishSyncAsset(const QString& assetId)
{
    const Private::SyncAssetCtx ctx = d->syncCtx.take(assetId);
    ++d->syncProcessed;

    const Private::SyncJob* job = nullptr;

    for (const Private::SyncJob& candidate : std::as_const(d->syncJobs))
    {
        if (candidate.assetId == assetId)
        {
            job = &candidate;
            break;
        }
    }

    if (ctx.failed || !job)
    {
        ++d->syncFailed;
    }
    else
    {
        if (ctx.wrote)
        {
            ++d->syncUpdated;
        }
        else
        {
            ++d->syncClean;
        }

        // Snapshot the post-sync state: server fields as they
        // now stand (its prior values with our plan applied).
        SyncFields after = ctx.server.fields;

        if (ctx.plan.setRating)
        {
            after.rating = ctx.plan.rating;
        }

        if (ctx.plan.setDescription)
        {
            after.description = ctx.plan.description;
        }

        if (ctx.plan.setGps)
        {
            after.hasGps    = true;
            after.latitude  = ctx.plan.latitude;
            after.longitude = ctx.plan.longitude;
        }

        for (const QString& tag : ctx.plan.tagsToAdd)
        {
            if (!after.tags.contains(tag))
            {
                after.tags << tag;
            }
        }

        storeSnapshot(assetId, after, ctx.server.updatedAt,
                      ctx.server.checksum, job->dk);
    }

    checkSyncPhaseDone();
}

void ImmichWindow::checkSyncPhaseDone()
{
    if (!d->syncing)
    {
        return;
    }

    const bool allLaunched = d->syncAborted || d->syncNextIndex >= d->syncJobs.size();

    if (allLaunched && d->syncFetchInFlight == 0 && d->syncPatchInFlight == 0)
    {
        startSyncTags();
    }
}

void ImmichWindow::startSyncTags()
{
    if (d->syncAborted || d->syncTagAdds.isEmpty())
    {
        finishSync();
        return;
    }

    d->tagsPurpose = Private::TagsPurpose::Sync;
    d->status->setText(i18n("Syncing tags..."));
    d->talker->listTags();
}

void ImmichWindow::handleSyncTagList(const QList<ImmichTag>& tags)
{
    if (!d->syncing)
    {
        return;
    }

    for (const ImmichTag& tag : tags)
    {
        d->syncTagIds.insert(tag.name, tag.id);
    }

    d->syncTagsToCreate.clear();

    for (auto it = d->syncTagAdds.constBegin(); it != d->syncTagAdds.constEnd(); ++it)
    {
        if (!d->syncTagIds.contains(it.key()))
        {
            d->syncTagsToCreate.append(it.key());
        }
    }

    nextTagCreate();
}

void ImmichWindow::nextTagCreate()
{
    if (!d->syncTagsToCreate.isEmpty())
    {
        d->talker->createTag(d->syncTagsToCreate.constFirst());
        return;
    }

    d->syncBulkQueue.clear();

    for (auto it = d->syncTagAdds.constBegin(); it != d->syncTagAdds.constEnd(); ++it)
    {
        const QString id = d->syncTagIds.value(it.key());

        if (!id.isEmpty())
        {
            d->syncBulkQueue.append(qMakePair(id, it.value()));
        }
    }

    applyNextBulkTag();
}

void ImmichWindow::slotTagCreated(const QString& name, const QString& id, int httpStatus)
{
    if (!d->syncing)
    {
        return;
    }

    d->syncTagsToCreate.removeAll(name);

    if (httpStatus == 403)
    {
        d->syncPermsDenied = true;
        d->syncAborted     = true;
        finishSync();
        return;
    }

    if (id.isEmpty() || httpStatus < 200 || httpStatus >= 300)
    {
        ++d->syncFailed;
    }
    else
    {
        d->syncTagIds.insert(name, id);
    }

    nextTagCreate();
}

void ImmichWindow::applyNextBulkTag()
{
    if (d->syncBulkQueue.isEmpty())
    {
        finishSync();
        return;
    }

    const QPair<QString, QStringList> item = d->syncBulkQueue.constFirst();
    d->talker->bulkTagAssets(item.first, item.second);
}

void ImmichWindow::slotBulkTagged(const QString& /*tagId*/, int httpStatus)
{
    if (!d->syncing)
    {
        return;
    }

    if (!d->syncBulkQueue.isEmpty())
    {
        d->syncBulkQueue.removeFirst();
    }

    if (httpStatus == 403)
    {
        d->syncPermsDenied = true;
        d->syncAborted     = true;
        finishSync();
        return;
    }

    if (httpStatus < 200 || httpStatus >= 300)
    {
        ++d->syncFailed;
    }

    applyNextBulkTag();
}

void ImmichWindow::finishSync()
{
    d->syncing = false;
    d->syncJobs.clear();
    d->syncTagAdds.clear();
    d->syncBulkQueue.clear();
    d->syncCtx.clear();
    printUploadSummary();
}

void ImmichWindow::storeSnapshot(const QString& assetId, const SyncFields& serverFields,
                                 const QString& serverUpdatedAt, const QString& checksum,
                                 const SyncFields& dkFields)
{
    SyncSnapshot snap;
    snap.checksum        = checksum;
    snap.serverUpdatedAt = serverUpdatedAt;
    snap.serverHash      = syncFieldHash(serverFields);
    snap.dkHash          = syncFieldHash(dkFields);
    d->syncState.insert(assetId, snap.toJson());
    saveSyncState(d->syncStatePath, d->syncState);
}

} // namespace DigikamGenericImmichPlugin
