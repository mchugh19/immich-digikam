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

#include <algorithm>
#include <cmath>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QComboBox>
#include <QDateTime>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QPixmap>
#include <QRadioButton>
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
#include "immichxmp.h"

namespace DigikamGenericImmichPlugin
{

class ImmichWindow::Private
{
public:

    DInfoInterface* iface   = nullptr;
    ImmichSession*  session = nullptr;
    ImmichTalker*   talker  = nullptr;
    Mode            mode    = Mode::Export;

    QLineEdit*    urlEdit     = nullptr;
    QLineEdit*    keyEdit     = nullptr;
    QLineEdit*    filterEdit  = nullptr;
    QRadioButton* albumsSrc   = nullptr;
    QRadioButton* tagsSrc     = nullptr;
    QTableWidget* sourceTable = nullptr;
    QListWidget*  previewList = nullptr;
    QComboBox*    previewSort = nullptr;
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

    // Import source + preview state. The listing machinery is
    // shared between the contents preview and a real download;
    // listingPurpose says which one a listing result belongs to.
    enum class ListingPurpose { None, Preview, Download };
    ListingPurpose        listingPurpose = Private::ListingPurpose::None;
    bool                  sourceIsTags   = false;
    QString               previewSourceId;
    QList<ImmichAssetRef> previewAssets;
    QHash<QString, QPixmap> thumbCache;
    QHash<QString, QPixmap> coverCache;
    int                   previewGen     = 0;

    // One serialized thumbnail pipeline serves both the preview
    // grid and the album covers in the source table.
    struct ThumbJob
    {
        bool    cover   = false;
        QString key;        // album id for covers, asset id for previews
        QString assetId;    // the asset whose thumbnail is fetched
    };
    QList<ThumbJob> thumbJobs;
    ThumbJob        currentThumb;
    bool            thumbBusy = false;

    // Metadata refresh queue for files skipped as already on
    // disk: (asset id, local target path).
    QList<QPair<QString, QString>> metaPending;
    int          metaTotal     = 0;
    int          metaDone      = 0;
    int          metaUpdated   = 0;

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

ImmichWindow::ImmichWindow(DInfoInterface* const iface, Mode mode, QWidget* const parent)
    : QDialog(parent),
      d(new Private)
{
    d->iface   = iface;
    d->mode    = mode;
    d->session = new ImmichSession;
    d->session->load();
    d->talker  = new ImmichTalker(d->session, this);

    setWindowTitle(d->mode == Mode::Export ? QStringLiteral("Export to Immich")
                                           : QStringLiteral("Import from Immich"));

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
    d->sourceTable      = new QTableWidget(0, 3);
    d->sourceTable->setHorizontalHeaderLabels({ QStringLiteral("Album"),
                                                QStringLiteral("Items"),
                                                QStringLiteral("Date range") });
    d->sourceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->sourceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    d->sourceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->sourceTable->verticalHeader()->setVisible(false);
    d->sourceTable->verticalHeader()->setDefaultSectionSize(58);
    d->sourceTable->setIconSize(QSize(72, 54));
    d->sourceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    d->sourceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    d->sourceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    d->sourceTable->setShowGrid(false);
    d->filterEdit       = new QLineEdit;
    d->filterEdit->setPlaceholderText(QStringLiteral("Filter..."));
    d->filterEdit->setClearButtonEnabled(true);
    d->albumsSrc        = new QRadioButton(QStringLiteral("Albums"));
    d->tagsSrc          = new QRadioButton(QStringLiteral("Tags"));
    d->albumsSrc->setChecked(true);
    d->previewList      = new QListWidget;
    d->previewList->setViewMode(QListWidget::IconMode);
    d->previewList->setIconSize(QSize(96, 96));
    d->previewList->setResizeMode(QListWidget::Adjust);
    d->previewList->setMovement(QListWidget::Static);
    d->previewList->setSpacing(4);
    d->previewList->setUniformItemSizes(true);
    d->previewList->setWordWrap(true);
    d->previewSort      = new QComboBox;
    d->previewSort->addItems({ QStringLiteral("Date (oldest first)"),
                               QStringLiteral("Date (newest first)"),
                               QStringLiteral("Name") });
    d->targetLabel      = new QLabel(QStringLiteral("Upload target: no album (library only)"));
    d->progress         = new QProgressBar;
    d->progress->setVisible(false);
    d->status           = new QLabel;

    // Each tool shows only its own direction's actions. The source
    // switch and the contents preview belong to the import tool;
    // the filter serves the source list in both.
    d->targetLabel->setVisible(d->mode == Mode::Export);
    d->uploadBtn->setVisible(d->mode == Mode::Export);
    d->downloadBtn->setVisible(d->mode == Mode::Import);

    auto* const sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(QStringLiteral("Import from:")));
    sourceRow->addWidget(d->albumsSrc);
    sourceRow->addWidget(d->tagsSrc);
    sourceRow->addStretch();

    auto* const previewRow = new QHBoxLayout;
    previewRow->addWidget(new QLabel(QStringLiteral("Contents:")));
    previewRow->addWidget(d->previewSort);
    previewRow->addStretch();

    auto* const layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(saveBtn);
    layout->addWidget(d->refreshBtn);
    layout->addWidget(d->newAlbumBtn);
    layout->addWidget(d->filterEdit);

    if (d->mode == Mode::Import)
    {
        layout->addLayout(sourceRow);
    }

    layout->addWidget(d->sourceTable);

    if (d->mode == Mode::Import)
    {
        layout->addLayout(previewRow);
        layout->addWidget(d->previewList);
    }

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
    connect(d->sourceTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int)
            {
                QTableWidgetItem* const item = (row >= 0) ? d->sourceTable->item(row, 0)
                                                          : nullptr;
                d->targetLabel->setText(item
                    ? QStringLiteral("Upload target: %1").arg(item->data(Qt::UserRole + 1).toString())
                    : QStringLiteral("Upload target: no album (library only)"));

                if (item && d->mode == Mode::Import)
                {
                    startPreview(item->data(Qt::UserRole).toString());
                }
            });
    connect(d->talker, &ImmichTalker::signalTags, this, &ImmichWindow::slotTags);
    connect(d->talker, &ImmichTalker::signalThumbnail, this, &ImmichWindow::slotThumbnail);
    connect(d->albumsSrc, &QRadioButton::toggled, this, &ImmichWindow::slotSourceChanged);
    connect(d->tagsSrc, &QRadioButton::toggled, this, &ImmichWindow::slotSourceChanged);
    connect(d->filterEdit, &QLineEdit::textChanged, this, &ImmichWindow::slotFilterChanged);
    connect(d->previewSort, &QComboBox::currentIndexChanged, this, &ImmichWindow::slotPreviewSortChanged);
    connect(d->downloadBtn, &QPushButton::clicked, this, &ImmichWindow::slotDownloadAlbum);
    connect(d->cancelBtn, &QPushButton::clicked, this, &ImmichWindow::slotCancelTransfer);
    connect(d->talker, &ImmichTalker::signalAlbums, this, &ImmichWindow::slotAlbums);
    connect(d->talker, &ImmichTalker::signalAlbumAssets, this, &ImmichWindow::slotAlbumAssets);
    connect(d->talker, &ImmichTalker::signalDownloadFinished, this, &ImmichWindow::slotDownloadFinished);
    connect(d->talker, &ImmichTalker::signalAssetRecord, this, &ImmichWindow::slotAssetRecord);
    connect(d->talker, &ImmichTalker::signalMetaFinished, this, &ImmichWindow::slotMetaFinished);
    connect(d->talker, &ImmichTalker::signalMetaRecord, this, &ImmichWindow::slotMetaRecord);
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

int ImmichWindow::findSourceRow(const QString& id) const
{
    for (int row = 0 ; row < d->sourceTable->rowCount() ; ++row)
    {
        if (d->sourceTable->item(row, 0)->data(Qt::UserRole).toString() == id)
        {
            return row;
        }
    }

    return -1;
}

QString ImmichWindow::currentSourceId() const
{
    const int row = d->sourceTable->currentRow();
    return (row >= 0) ? d->sourceTable->item(row, 0)->data(Qt::UserRole).toString()
                      : QString();
}

QString ImmichWindow::currentSourceName() const
{
    const int row = d->sourceTable->currentRow();
    return (row >= 0) ? d->sourceTable->item(row, 0)->data(Qt::UserRole + 1).toString()
                      : QString();
}

void ImmichWindow::addSourceRow(const QString& id, const QString& name,
                                const QString& countText, const QString& rangeText,
                                const QString& thumbnailAssetId)
{
    const int row = d->sourceTable->rowCount();
    d->sourceTable->insertRow(row);

    auto* const nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, id);
    nameItem->setData(Qt::UserRole + 1, name);
    d->sourceTable->setItem(row, 0, nameItem);

    auto* const countItem = new QTableWidgetItem(countText);
    countItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d->sourceTable->setItem(row, 1, countItem);

    d->sourceTable->setItem(row, 2, new QTableWidgetItem(rangeText));

    if (!thumbnailAssetId.isEmpty())
    {
        const auto cached = d->coverCache.constFind(id);

        if (cached != d->coverCache.constEnd())
        {
            nameItem->setIcon(QIcon(*cached));
        }
        else
        {
            queueThumb(true, id, thumbnailAssetId);
        }
    }
}

void ImmichWindow::selectSource(const QString& id)
{
    const int row = findSourceRow(id);

    if (row >= 0)
    {
        d->sourceTable->setCurrentCell(row, 0);
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
    if (d->thumbBusy || d->thumbJobs.isEmpty())
    {
        return;
    }

    d->currentThumb = d->thumbJobs.takeFirst();
    d->thumbBusy    = true;
    d->talker->fetchThumbnail(d->currentThumb.assetId);
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
}

void ImmichWindow::slotRefreshAlbums()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());

    if (d->mode == Mode::Import && d->sourceIsTags)
    {
        d->talker->listTags();
    }
    else
    {
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
    d->newAlbumBtn->setEnabled(!tags);
    d->sourceTable->setRowCount(0);
    d->previewList->clear();
    d->previewAssets.clear();
    dropThumbJobs(false);
    dropThumbJobs(true);
    ++d->previewGen;
    slotRefreshAlbums();
}

void ImmichWindow::applyFilter()
{
    const QString text = d->filterEdit->text();

    for (int row = 0 ; row < d->sourceTable->rowCount() ; ++row)
    {
        const QString name = d->sourceTable->item(row, 0)->data(Qt::UserRole + 1).toString();
        d->sourceTable->setRowHidden(row, !text.isEmpty()
                                          && !name.contains(text, Qt::CaseInsensitive));
    }

    if (d->mode == Mode::Import)
    {
        for (int i = 0 ; i < d->previewList->count() ; ++i)
        {
            QListWidgetItem* const item = d->previewList->item(i);
            item->setHidden(!text.isEmpty()
                            && !item->text().contains(text, Qt::CaseInsensitive));
        }
    }
}

void ImmichWindow::slotFilterChanged(const QString&)
{
    applyFilter();
}

void ImmichWindow::slotTags(const QList<ImmichTag>& tags)
{
    const QString restoreId = currentSourceId();

    d->sourceTable->setRowCount(0);
    d->sourceTable->setHorizontalHeaderItem(0, new QTableWidgetItem(QStringLiteral("Tag")));

    QList<ImmichTag> sorted = tags;
    std::sort(sorted.begin(), sorted.end(),
              [](const ImmichTag& a, const ImmichTag& b)
              {
                  return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
              });

    for (const ImmichTag& tag : sorted)
    {
        addSourceRow(tag.id, tag.name, QString(), QString(), QString());
    }

    selectSource(restoreId);
    applyFilter();
    d->status->setText(QStringLiteral("%1 tags").arg(tags.size()));
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
    const QString restoreId = !d->pendingSelectId.isEmpty() ? d->pendingSelectId
                                                            : currentSourceId();
    d->pendingSelectId.clear();

    d->sourceTable->setRowCount(0);
    d->sourceTable->setHorizontalHeaderItem(0, new QTableWidgetItem(QStringLiteral("Album")));

    QList<ImmichAlbum> sorted = albums;
    std::sort(sorted.begin(), sorted.end(),
              [](const ImmichAlbum& a, const ImmichAlbum& b)
              {
                  return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
              });

    for (const ImmichAlbum& album : sorted)
    {
        addSourceRow(album.id, album.name,
                     QStringLiteral("%L1").arg(album.assetCount),
                     dateRangeText(album.startDate, album.endDate),
                     album.thumbnailAssetId);
    }

    selectSource(restoreId);
    applyFilter();
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

    d->targetAlbumId = currentSourceId();

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
    d->metaPending.clear();
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

    const QString sourceId = currentSourceId();

    if (sourceId.isEmpty())
    {
        d->status->setText(QStringLiteral("Select an album or tag to download."));
        return;
    }

    const QString startDir = d->session->downloadDir().isEmpty() ? QDir::homePath()
                                                                 : d->session->downloadDir();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Download album to"), startDir);

    if (dir.isEmpty())
    {
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
    ++d->previewGen;
    dropThumbJobs(false);

    setTransferButtonsEnabled(false);
    d->cancelBtn->setVisible(true);
    d->status->setText(QStringLiteral("Listing assets..."));

    if (d->sourceIsTags)
    {
        d->talker->listTagAssets(sourceId);
    }
    else
    {
        d->talker->listAlbumAssets(sourceId);
    }
}

void ImmichWindow::startPreview(const QString& sourceId)
{
    if (d->uploading || d->downloading || sourceId.isEmpty()
        || d->listingPurpose != Private::ListingPurpose::None)
    {
        return;
    }

    d->previewSourceId = sourceId;
    ++d->previewGen;
    d->previewList->clear();
    d->previewAssets.clear();
    dropThumbJobs(false);
    d->listingPurpose = Private::ListingPurpose::Preview;
    d->status->setText(QStringLiteral("Listing contents..."));

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

    for (const ImmichAssetRef& asset : sorted)
    {
        auto* const item = new QListWidgetItem(asset.fileName, d->previewList);
        item->setData(Qt::UserRole, asset.id);
        item->setToolTip(asset.fileCreatedAt.left(10));

        const auto cached = d->thumbCache.constFind(asset.id);

        if (cached != d->thumbCache.constEnd())
        {
            item->setIcon(QIcon(*cached));
        }
        else
        {
            queueThumb(false, asset.id, asset.id);
        }
    }

    applyFilter();
    d->status->setText(QStringLiteral("%1 items").arg(sorted.size()));
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
    d->thumbBusy = false;
    const Private::ThumbJob job = d->currentThumb;

    if (!bytes.isEmpty())
    {
        QPixmap pix;

        if (pix.loadFromData(bytes))
        {
            if (job.cover)
            {
                d->coverCache.insert(job.key, pix);
                const int row = findSourceRow(job.key);

                if (row >= 0)
                {
                    d->sourceTable->item(row, 0)->setIcon(QIcon(pix));
                }
            }
            else
            {
                d->thumbCache.insert(assetId, pix);

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

    pumpThumbs();
}

void ImmichWindow::slotAlbumAssets(const QList<ImmichAssetRef>& assets)
{
    if (d->listingPurpose == Private::ListingPurpose::Preview)
    {
        d->listingPurpose = Private::ListingPurpose::None;
        d->previewAssets  = assets;
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

    d->listingPurpose = Private::ListingPurpose::None;
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
            // Already downloaded by an earlier run: skip the file,
            // but queue a metadata refresh so Immich-side tag,
            // rating or caption changes still reach digiKam.
            claimed.insert(name);
            d->metaPending.append(qMakePair(asset.id, d->dlDir + QLatin1Char('/') + name));
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
                d->metaPending.append(qMakePair(asset.id, d->dlDir + QLatin1Char('/') + name));
                continue;
            }
        }

        claimed.insert(name);
        ImmichAssetRef target = asset;
        target.fileName       = name;
        d->dlPending.append(target);
    }

    d->metaTotal = d->metaPending.size();

    d->progress->setRange(0, d->dlTotal);
    d->progress->setValue(d->dlDone);
    d->progress->setVisible(true);

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

    d->status->setText(QStringLiteral("Refreshing metadata for existing files (%1 of %2)")
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

    d->status->setText(QStringLiteral("Downloading %1 (%2 of %3)")
                           .arg(asset.fileName)
                           .arg(d->dlDone + 1)
                           .arg(d->dlTotal));

    d->talker->downloadAsset(asset.id, d->dlDir + QLatin1Char('/') + asset.fileName);
}

void ImmichWindow::slotAssetRecord(const QString& assetId, const QString& targetFile,
                                   const QJsonObject& record)
{
    writeRecordSidecar(targetFile, record);
    slotDownloadFinished(assetId, true);
}

void ImmichWindow::slotMetaRecord(const QString& assetId, const QString& targetFile,
                                  const QJsonObject& record)
{
    slotMetaFinished(assetId, writeRecordSidecar(targetFile, record));
}

bool ImmichWindow::writeRecordSidecar(const QString& targetFile, const QJsonObject& record)
{
    // Generate a sidecar from an Immich asset record so digiKam
    // imports the tags, rating, caption and GPS together with the
    // photo. Tag names live in the record's tags array ("value" in
    // current servers, "name" in older ones); rating and friends
    // live in exifInfo. Returns false when the record carries no
    // metadata worth writing (any local sidecar is left alone).
    const QJsonObject exif = record.value(QStringLiteral("exifInfo")).toObject();

    QStringList tags;

    for (const QJsonValue& value : record.value(QStringLiteral("tags")).toArray())
    {
        const QJsonObject tag = value.toObject();
        QString           name = tag.value(QStringLiteral("value")).toString();

        if (name.isEmpty())
        {
            name = tag.value(QStringLiteral("name")).toString();
        }

        if (!name.isEmpty() && !tags.contains(name))
        {
            tags << name;
        }
    }

    int rating = exif.value(QStringLiteral("rating")).toInt();

    if (rating == 0)
    {
        rating = record.value(QStringLiteral("rating")).toInt();
    }

    const QString    description = exif.value(QStringLiteral("description")).toString();
    const QJsonValue lat         = exif.value(QStringLiteral("latitude"));
    const QJsonValue lon         = exif.value(QStringLiteral("longitude"));
    const bool       hasGps      = lat.isDouble() && lon.isDouble();

    if (!tags.isEmpty() || rating != 0 || !description.isEmpty() || hasGps)
    {
        const QString packet = buildSidecarPacket(tags, rating, description, hasGps,
                                                  lat.toDouble(), lon.toDouble());
        QFile         file(targetFile + QStringLiteral(".xmp"));

        if (file.open(QIODevice::WriteOnly))
        {
            return file.write(packet.toUtf8()) == packet.toUtf8().size();
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
    d->cancelBtn->setVisible(false);

    if (d->cancelling)
    {
        d->cancelling = false;
        d->status->setText(QStringLiteral("Download cancelled after %1 of %2 assets.")
                               .arg(d->dlDone)
                               .arg(d->dlTotal));
        return;
    }

    QString summary = QStringLiteral("Downloaded %1 assets (%2 already on disk, %3 failed) to %4")
                          .arg(d->dlDownloaded)
                          .arg(d->dlSkipped)
                          .arg(d->dlFailed)
                          .arg(d->dlDir);

    if (d->metaUpdated > 0)
    {
        summary += QStringLiteral("; metadata refreshed for %1 existing files").arg(d->metaUpdated);
    }

    d->status->setText(summary);
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
            const QString xmp = buildSidecarPacket(tags, rating, comment,
                                                   hasGps,
                                                   hasGps ? item.latitude() : 0.0,
                                                   hasGps ? item.longitude() : 0.0);

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
