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

namespace
{

// "Feb – Oct 2026" in one year, "Dec 1997 – Aug 2026" across
// years, "June 2026" inside one month: the way Immich's own album
// view renders an album's date range.
QString dateRangeText(const QString& startIso, const QString& endIso)
{
    const QDateTime start = QDateTime::fromString(startIso, Qt::ISODate);
    const QDateTime end   = QDateTime::fromString(endIso, Qt::ISODate);

    if (!start.isValid() && !end.isValid())
    {
        return QString();
    }

    const QDateTime from = start.isValid() ? start : end;
    const QDateTime to   = end.isValid() ? end : start;

    if (from.date().year() != to.date().year())
    {
        return QStringLiteral("%1 %2 – %3 %4")
            .arg(from.toString(QStringLiteral("MMM")),
                 from.toString(QStringLiteral("yyyy")),
                 to.toString(QStringLiteral("MMM")),
                 to.toString(QStringLiteral("yyyy")));
    }

    if (from.date().month() != to.date().month())
    {
        return QStringLiteral("%1 – %2 %3")
            .arg(from.toString(QStringLiteral("MMM")),
                 to.toString(QStringLiteral("MMM")),
                 from.toString(QStringLiteral("yyyy")));
    }

    return QStringLiteral("%1 %2")
        .arg(from.toString(QStringLiteral("MMMM")),
             from.toString(QStringLiteral("yyyy")));
}

} // namespace

QListWidgetItem* ImmichWindow::findAlbumItem(const QString& id) const
{
    for (int i = 0 ; i < d->albumGrid->count() ; ++i)
    {
        if (d->albumGrid->item(i)->data(Qt::UserRole).toString() == id)
        {
            return d->albumGrid->item(i);
        }
    }

    return nullptr;
}

QString ImmichWindow::currentSourceId() const
{
    // In the export tool, "Library only" is an explicit choice,
    // not just the absence of a selection.
    if (d->mode == Mode::Export && d->libraryRadio && !d->albumRadio->isChecked())
    {
        return QString();
    }

    QListWidgetItem* const item = d->sourceIsTags ? d->tagGrid->currentItem()
                                                  : d->albumGrid->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString ImmichWindow::currentSourceName() const
{
    if (d->mode == Mode::Export && d->libraryRadio && !d->albumRadio->isChecked())
    {
        return QString();
    }

    QListWidgetItem* const item = d->sourceIsTags ? d->tagGrid->currentItem()
                                                  : d->albumGrid->currentItem();
    return item ? item->data(Qt::UserRole + 1).toString() : QString();
}

void ImmichWindow::setAlbumCover(QListWidgetItem* item, const QPixmap& pix)
{
    QWidget* const tile = item ? d->albumGrid->itemWidget(item) : nullptr;
    QLabel* const cover = tile ? tile->findChild<QLabel*>(QStringLiteral("cover"))
                               : nullptr;

    if (cover)
    {
        // Fill the cover frame, cropping the overflow, the way
        // Immich's album grid presents covers of any aspect.
        cover->setPixmap(pix.scaled(cover->size(), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
    }
}

void ImmichWindow::addAlbumTile(const QString& id, const QString& name,
                                const QString& countText, const QString& rangeText,
                                const QString& thumbnailAssetId)
{
    auto* const item = new QListWidgetItem(d->albumGrid);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, name);
    item->setSizeHint(QSize(208, 248));
    item->setToolTip(name);

    auto* const tile = new QWidget;
    auto* const tileLayout = new QVBoxLayout(tile);
    tileLayout->setContentsMargins(6, 6, 6, 6);
    tileLayout->setSpacing(2);

    auto* const cover = new QLabel;
    cover->setObjectName(QStringLiteral("cover"));
    cover->setFixedSize(184, 138);
    cover->setAlignment(Qt::AlignCenter);
    cover->setStyleSheet(QStringLiteral("background: #2b2b2b; border-radius: 6px;"));
    tileLayout->addWidget(cover);

    auto* const nameLabel = new QLabel(name);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 2);
    nameLabel->setFont(nameFont);
    nameLabel->setWordWrap(true);
    tileLayout->addWidget(nameLabel);

    if (!rangeText.isEmpty())
    {
        auto* const rangeLabel = new QLabel(rangeText);
        rangeLabel->setObjectName(QStringLiteral("sub"));
        rangeLabel->setStyleSheet(QStringLiteral("color: #9a9a9a;"));
        tileLayout->addWidget(rangeLabel);
    }

    if (!countText.isEmpty())
    {
        auto* const countLabel = new QLabel(i18n("%1 items").arg(countText));
        countLabel->setObjectName(QStringLiteral("sub"));
        countLabel->setStyleSheet(QStringLiteral("color: #9a9a9a;"));
        tileLayout->addWidget(countLabel);
    }

    tileLayout->addStretch();
    d->albumGrid->setItemWidget(item, tile);

    if (!thumbnailAssetId.isEmpty())
    {
        const auto cached = d->coverCache.constFind(id);

        if (cached != d->coverCache.constEnd())
        {
            setAlbumCover(item, *cached);
        }
        else
        {
            queueThumb(true, id, thumbnailAssetId);
        }
    }
}

void ImmichWindow::selectSource(const QString& id)
{
    if (QListWidgetItem* const item = findAlbumItem(id))
    {
        d->albumGrid->setCurrentItem(item);
    }
}

void ImmichWindow::refreshTileStyles()
{
    // The tiles' secondary labels (date range, item count) are a
    // fixed mid-grey, which reads fine on the dark tile but washes
    // out on the bright selection highlight. Restyle them for
    // whichever tile is current.
    for (int i = 0 ; i < d->albumGrid->count() ; ++i)
    {
        QListWidgetItem* const item = d->albumGrid->item(i);
        QWidget* const tile = d->albumGrid->itemWidget(item);

        if (!tile)
        {
            continue;
        }

        const bool selected = (item == d->albumGrid->currentItem());
        const QList<QLabel*> subs = tile->findChildren<QLabel*>(QStringLiteral("sub"));

        for (QLabel* const sub : subs)
        {
            sub->setStyleSheet(selected ? QStringLiteral("color: rgba(255, 255, 255, 0.8);")
                                        : QStringLiteral("color: #9a9a9a;"));
        }
    }
}

void ImmichWindow::setAlbumLoading(bool loading)
{
    d->albumsLoading = loading;

    if (!d->albumStack || !d->albumLoadingOverlay)
    {
        return;
    }

    d->albumLoadingOverlay->setVisible(loading);
    d->albumStack->setCurrentWidget(loading ? static_cast<QWidget*>(d->albumLoadingOverlay)
                                            : static_cast<QWidget*>(d->albumGrid));
    d->albumSpinner->toggleTimer(loading);
}

void ImmichWindow::slotRefreshAlbums()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->session->clearListings();

    if (d->mode == Mode::Import && d->sourceIsTags)
    {
        d->talker->listTags();
    }
    else
    {
        setAlbumLoading(true);
        d->talker->listAlbums();
    }
}

void ImmichWindow::slotSourceChanged()
{
    const bool tags = d->tagsSrc->isChecked();

    if (tags == d->sourceIsTags)
    {
        return;
    }

    d->sourceIsTags = tags;

    // With Tags as the source there is no new album to create,
    // so in the export tool the button is hidden rather than
    // shown disabled. (The import tool never shows it.)
    if (d->mode == Mode::Export)
    {
        d->newAlbumBtn->setVisible(!tags && d->albumRadio->isChecked());
    }

    d->albumArea->setVisible(!tags);
    d->tagGrid->setVisible(tags);
    d->albumGrid->clear();
    d->tagGrid->clear();
    d->previewList->clear();
    d->previewAssets.clear();
    dropThumbJobs(false);
    dropThumbJobs(true);
    ++d->previewGen;

    if (!d->downloading)
    {
        // A listing for the old source may never report back (the
        // talker drops superseded pages), so do not let its purpose
        // block previews of the new source.
        d->listingPurpose = Private::ListingPurpose::None;
    }

    slotRefreshAlbums();
}

void ImmichWindow::applyFilter()
{
    const QString text = d->filterEdit->text();

    for (int i = 0 ; i < d->albumGrid->count() ; ++i)
    {
        QListWidgetItem* const item = d->albumGrid->item(i);
        const QString name = item->data(Qt::UserRole + 1).toString();
        item->setHidden(!text.isEmpty()
                        && !name.contains(text, Qt::CaseInsensitive));
    }

    for (int i = 0 ; i < d->tagGrid->count() ; ++i)
    {
        QListWidgetItem* const item = d->tagGrid->item(i);
        item->setHidden(!text.isEmpty()
                        && !item->text().contains(text, Qt::CaseInsensitive));
    }

    // The contents preview is deliberately NOT filtered: the
    // filter is for finding a source, and hiding the selected
    // album's photos because their file names do not match the
    // album search text reads as "the preview did not load"
    // (an album of PXL_*.jpg under a "test" filter showed
    // "8 items" over an empty grid).
}

void ImmichWindow::slotFilterChanged(const QString&)
{
    applyFilter();
}

void ImmichWindow::slotTags(const QList<ImmichTag>& tags)
{
    if (d->tagsPurpose == Private::TagsPurpose::Sync)
    {
        d->tagsPurpose = Private::TagsPurpose::Ui;
        handleSyncTagList(tags);
        return;
    }

    // A listing came back: the connection works.
    setConnected(true);

    const QString restoreId = currentSourceId();

    d->tagGrid->clear();

    QList<ImmichTag> sorted = tags;
    std::sort(sorted.begin(), sorted.end(),
              [](const ImmichTag& a, const ImmichTag& b)
              {
                  return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
              });

    const QIcon tagIcon(QStringLiteral(":/dk-tag.svg"));

    for (const ImmichTag& tag : sorted)
    {
        auto* const item = new QListWidgetItem(tagIcon, tag.name, d->tagGrid);
        item->setData(Qt::UserRole, tag.id);
        item->setData(Qt::UserRole + 1, tag.name);
        item->setTextAlignment(Qt::AlignHCenter);
    }

    for (int i = 0 ; i < d->tagGrid->count() ; ++i)
    {
        if (d->tagGrid->item(i)->data(Qt::UserRole).toString() == restoreId)
        {
            d->tagGrid->setCurrentItem(d->tagGrid->item(i));
            break;
        }
    }

    applyFilter();
    d->status->setText(i18n("%1 tags").arg(tags.size()));
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
    const QString name = QInputDialog::getText(this, i18n("New album"),
                                               i18n("Album name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();

    if (!ok || name.isEmpty())
    {
        return;
    }

    d->status->setText(i18n("Creating album..."));
    d->talker->createAlbum(name);
}

void ImmichWindow::slotAlbumCreated(const ImmichAlbum& album)
{
    // Refresh the list and land the selection on the new album,
    // ready to be the upload target.
    d->pendingSelectId = album.id;
    d->status->setText(i18n("Album '%1' created.").arg(album.name));
    setAlbumLoading(true);
    d->talker->listAlbums();
}

void ImmichWindow::slotAlbums(const QList<ImmichAlbum>& albums)
{
    setAlbumLoading(false);

    // A listing came back: the connection works.
    setConnected(true);

    const QString restoreId = !d->pendingSelectId.isEmpty() ? d->pendingSelectId
                                                            : currentSourceId();
    d->pendingSelectId.clear();

    d->albumGrid->clear();

    QList<ImmichAlbum> sorted = albums;
    std::sort(sorted.begin(), sorted.end(),
              [](const ImmichAlbum& a, const ImmichAlbum& b)
              {
                  return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
              });

    for (const ImmichAlbum& album : sorted)
    {
        addAlbumTile(album.id, album.name,
                     QStringLiteral("%L1").arg(album.assetCount),
                     dateRangeText(album.startDate, album.endDate),
                     album.thumbnailAssetId);
    }

    selectSource(restoreId);
    applyFilter();

    if (d->mode == Mode::Export)
    {
        // The album count is not the interesting number here;
        // the status reports the upload set instead.
        updateExportStatus();
    }
    else
    {
        d->status->setText(i18n("%1 albums").arg(albums.size()));
    }
}

} // namespace DigikamGenericImmichPlugin
