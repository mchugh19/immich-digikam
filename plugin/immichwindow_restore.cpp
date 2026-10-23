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

void ImmichWindow::startRestoreOrSync()
{
    if (d->replaces.isEmpty())
    {
        maybeStartSync();
        return;
    }

    // Replaced photos: put each new copy into every album its
    // predecessor belonged to, then stack it as the new primary
    // over the predecessor (the old version stays in the
    // library, in the stack). Only when all of that has landed
    // does the metadata sync start.
    d->restoring    = true;
    d->restoreQueue = d->replaces;
    d->status->setText(i18n("Replacing %1 edited photo(s) in Immich...")
                           .arg(d->restoreQueue.size()));
    restoreNext();
}

void ImmichWindow::restoreNext()
{
    if (d->restoreQueue.isEmpty())
    {
        // Old copies headed for the trash, whether the user
        // chose overwrite or stacking was refused: a replace
        // never leaves two loose copies behind.
        const QStringList toTrash = d->restoreTrashQueue + d->overwriteQueue;

        if (!toTrash.isEmpty())
        {
            d->talker->deleteAssets(toTrash);
            return;
        }

        d->restoring = false;
        d->replaces.clear();
        maybeStartSync();
        return;
    }

    d->restoreWaitingAlbums = true;
    d->talker->listAlbumsForAsset(d->restoreQueue.constFirst().oldId);
}

void ImmichWindow::slotRestoreAlbums(const QString& assetId, const QStringList& albumIds)
{
    d->restoreWaitingAlbums = false;

    if (!d->restoring || d->restoreQueue.isEmpty()
        || d->restoreQueue.constFirst().oldId != assetId)
    {
        return;
    }

    d->restoreAlbums = albumIds;
    restoreAdvanceAdd();
}

void ImmichWindow::restoreAdvanceAdd()
{
    if (d->restoreQueue.isEmpty())
    {
        restoreNext();
        return;
    }

    if (d->restoreAlbums.isEmpty())
    {
        restoreStackStep();
        return;
    }

    const QString albumId = d->restoreAlbums.takeFirst();
    d->talker->addToAlbum(albumId, { d->restoreQueue.constFirst().newId });
}

void ImmichWindow::restoreStackStep()
{
    if (d->restoreQueue.isEmpty())
    {
        restoreNext();
        return;
    }

    if (!d->replaceByStack)
    {
        // The user chose overwrite: the old copy goes to the
        // trash instead of into a stack.
        const Private::ReplaceCtx ctx = d->restoreQueue.takeFirst();
        d->overwriteQueue.append(ctx.oldId);
        restoreNext();
        return;
    }

    const Private::ReplaceCtx& ctx = d->restoreQueue.constFirst();

    if (ctx.stackId.isEmpty())
    {
        // First edit of this photo: a fresh two-member stack,
        // the edit as primary.
        d->talker->createStack({ ctx.newId, ctx.oldId });
    }
    else
    {
        // Already versioned: fetch the stack so the restack can
        // carry every earlier version along.
        d->talker->fetchStack(ctx.stackId);
    }
}

void ImmichWindow::slotStackFetched(const QString& stackId, const QStringList& memberIds,
                                    int httpStatus)
{
    if (!d->restoring || d->restoreQueue.isEmpty()
        || d->restoreQueue.constFirst().stackId != stackId)
    {
        return;
    }

    const Private::ReplaceCtx& ctx = d->restoreQueue.constFirst();
    QStringList members { ctx.newId };

    if (httpStatus >= 200 && httpStatus < 300)
    {
        for (const QString& id : memberIds)
        {
            if (id != ctx.newId)
            {
                members.append(id);
            }
        }
    }

    if (members.size() < 2)
    {
        members.append(ctx.oldId);
    }

    // Creating a stack with already-stacked assets moves them
    // into the new one, so the whole version history ends up
    // under the newest edit as primary.
    d->talker->createStack(members);
}

void ImmichWindow::slotStackCreated(const QString& firstAssetId, const QString& stackId,
                                    int httpStatus)
{
    if (!d->restoring || d->restoreQueue.isEmpty()
        || d->restoreQueue.constFirst().newId != firstAssetId)
    {
        return;
    }

    const Private::ReplaceCtx ctx = d->restoreQueue.takeFirst();

    if (httpStatus >= 200 && httpStatus < 300 && !stackId.isEmpty())
    {
        ++d->stackedCount;
    }
    else
    {
        d->restoreTrashQueue.append(ctx.oldId);
    }

    restoreNext();
}

void ImmichWindow::slotAssetsDeleted(int httpStatus)
{
    d->restoring = false;

    if (httpStatus >= 200 && httpStatus < 300)
    {
        d->trashFallbackCount = d->restoreTrashQueue.size();
        d->overwriteCount     = d->overwriteQueue.size();

        for (const QString& oldId : std::as_const(d->restoreTrashQueue))
        {
            d->syncState.remove(oldId);
        }

        for (const QString& oldId : std::as_const(d->overwriteQueue))
        {
            d->syncState.remove(oldId);
        }

        saveSyncState(d->syncStatePath, d->syncState);
    }

    d->restoreTrashQueue.clear();
    d->overwriteQueue.clear();
    d->replaces.clear();
    d->restoreQueue.clear();
    maybeStartSync();
}

} // namespace DigikamGenericImmichPlugin
