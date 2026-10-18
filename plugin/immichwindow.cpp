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
#include <QStandardPaths>
#include <QVBoxLayout>

// Local includes

#include "dinfointerface.h"
#include "dmetadata.h"
#include "immichsession.h"
#include "immichsync.h"
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
    QListWidget*  albumGrid   = nullptr;
    QListWidget*  tagGrid     = nullptr;
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
    QString               listingKey;
    QList<ImmichAssetRef> previewAssets;
    // Contents listings by source ("album:<id>" / "tag:<id>"),
    // for the session; Refresh clears it. Thumbnails are cached
    // on disk instead (thumbCacheDir), across sessions.
    QHash<QString, QList<ImmichAssetRef>> listingCache;
    QString               thumbCacheDir;
    QHash<QString, QPixmap> thumbCache;
    QHash<QString, QPixmap> coverCache;
    int                   previewGen     = 0;

    // The thumbnail pipeline serves both the preview grid and
    // the album covers on the album tiles, a few fetches at a
    // time: one at a time made a 150-album view feel slow even
    // though each image is small.
    struct ThumbJob
    {
        bool    cover   = false;
        QString key;        // album id for covers, asset id for previews
        QString assetId;    // the asset whose thumbnail is fetched
    };
    QList<ThumbJob> thumbJobs;
    // Fetches in flight by asset id, with every job waiting on
    // that asset (an asset can be an album cover and a preview
    // item at once; one fetch satisfies both).
    QHash<QString, QList<ThumbJob>> thumbInFlight;
    static constexpr int maxThumbFetches = 4;

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

    // Metadata sync for files the upload check found already on
    // the server: their digiKam-side fields are compared with the
    // server's asset record and only the differences are written
    // (PUT /assets, tags API). Jobs carry the digiKam fields
    // captured when the check ran.
    struct SyncJob
    {
        QString    assetId;
        QString    localPath;
        SyncFields dk;
    };
    // Export identity matching: files the checksum check missed
    // get a name+capture-time+dimensions lookup before they are
    // allowed to upload as new photos.
    QList<QUrl>  identityQueue;
    QUrl         identityUrl;
    QString      identityCandidateId;
    bool         identityStrict = false;
    bool         identityDimsDiffer = false;
    bool         identityWaitingThumb = false;
    int          matchedSync   = 0;
    int          matchedMulti  = 0;
    int          replacedCount = 0;
    QHash<QString, QString> replaceOldByPath;
    struct ReplaceCtx
    {
        QString oldId;
        QString newId;
        QString localPath;
    };
    QList<ReplaceCtx> replaces;
    QList<ReplaceCtx> restoreQueue;
    QStringList  restoreAlbums;
    bool         restoring = false;
    QString      currentUploadPath;

    QList<SyncJob> syncJobs;
    int          syncUpdated   = 0;
    int          syncClean     = 0;
    int          syncFailed    = 0;
    int          syncConflicts = 0;
    bool         syncing       = false;
    bool         syncRan       = false;
    bool         syncPermsDenied = false;
    bool         syncAborted   = false;
    // Record fetches and scalar writes run a few at a time: a
    // strictly one-by-one sync made a large no-change export
    // crawl (one round trip per already-uploaded file, and
    // Immich has no bulk record endpoint to page instead).
    int          syncNextIndex = 0;
    int          syncFetchInFlight = 0;
    int          syncPatchInFlight = 0;
    int          syncProcessed = 0;
    static constexpr int maxSyncFetches = 6;
    struct SyncAssetCtx
    {
        SyncPlan     plan;
        ServerFields server;
        bool         failed = false;
        bool         wrote  = false;
    };
    QHash<QString, SyncAssetCtx> syncCtx;
    // Tag values to add, accumulated across the run's assets,
    // applied in bulk at the end: value -> asset ids.
    QMap<QString, QStringList>    syncTagAdds;
    QMap<QString, QString>        syncTagIds;
    QStringList                   syncTagsToCreate;
    QList<QPair<QString, QStringList>> syncBulkQueue;
    enum class TagsPurpose { Ui, Sync };
    TagsPurpose  tagsPurpose   = TagsPurpose::Ui;
    bool         albumAssigned = false;

    // Fields of the item currently uploading, kept so a sync
    // snapshot can be recorded when it lands.
    SyncFields   currentUploadFields;
    bool         haveUploadFields = false;

    // Per-asset sync snapshots (asset id -> SyncSnapshot JSON),
    // persisted across sessions; they are what tells a later run
    // whether the server side, the digiKam side, both, or
    // neither moved since the last sync.
    QJsonObject  syncState;
    QString      syncStatePath;
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

    // Sync snapshots live next to digiKam's other app data.
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    d->syncStatePath = appData + QStringLiteral("/immich-sync-state.json");
    d->syncState     = loadSyncState(d->syncStatePath);

    // On-disk thumbnail cache (album covers and contents
    // previews share the /assets/<id>/thumbnail endpoint), in
    // digiKam's cache directory so it survives restarts. Pruned
    // oldest-first when it grows past roughly 200 MB.
    d->thumbCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                       + QStringLiteral("/immich-thumbs");
    QDir().mkpath(d->thumbCacheDir);

    {
        const QFileInfoList files = QDir(d->thumbCacheDir).entryInfoList(
            QDir::Files, QDir::Time | QDir::Reversed);
        qint64 total = 0;

        for (const QFileInfo& file : files)
        {
            total += file.size();
        }

        for (const QFileInfo& file : files)
        {
            if (total <= 200LL * 1024 * 1024)
            {
                break;
            }

            total -= file.size();
            QFile::remove(file.absoluteFilePath());
        }
    }

    setWindowTitle(d->mode == Mode::Export ? QStringLiteral("Export to Immich")
                                           : QStringLiteral("Import from Immich"));

    // A functional default: the source tiles and the preview grid
    // are the working area, and both are cramped at the layout's
    // raw size hint. Growth goes to them via the layout stretch.
    resize(1040, 760);
    setMinimumSize(760, 560);

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
    d->downloadBtn      = new QPushButton(QStringLiteral("Download"));
    d->cancelBtn        = new QPushButton(QStringLiteral("Cancel transfer"));
    d->cancelBtn->setVisible(false);
    // Albums are tiles, like Immich's own Albums page: a large
    // cover, the name under it in a larger font, then the date
    // range and the item count in smaller type. Each tile is a
    // small widget hosted by a list item (see addAlbumTile).
    d->albumGrid        = new QListWidget;
    d->albumGrid->setViewMode(QListWidget::IconMode);
    d->albumGrid->setResizeMode(QListWidget::Adjust);
    d->albumGrid->setMovement(QListWidget::Static);
    d->albumGrid->setSpacing(6);
    d->albumGrid->setGridSize(QSize(208, 248));
    d->albumGrid->setUniformItemSizes(true);
    d->albumGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    // At least one whole tile row is always visible: the layout
    // stretch alone could squeeze the grid below a row's height,
    // leaving a clipped row and a scrollbar for a single row.
    d->albumGrid->setMinimumHeight(248 + 16);
    d->filterEdit       = new QLineEdit;
    d->filterEdit->setPlaceholderText(QStringLiteral("Filter..."));
    d->filterEdit->setClearButtonEnabled(true);
    d->albumsSrc        = new QRadioButton(QStringLiteral("Albums"));
    d->tagsSrc          = new QRadioButton(QStringLiteral("Tags"));
    d->albumsSrc->setChecked(true);
    // Tags carry no counts or dates: they get a simpler tile
    // grid like Immich's own Tags page, swapped in for the album
    // tiles when Tags is the source.
    d->tagGrid          = new QListWidget;
    d->tagGrid->setViewMode(QListWidget::IconMode);
    d->tagGrid->setIconSize(QSize(48, 48));
    d->tagGrid->setGridSize(QSize(132, 104));
    d->tagGrid->setResizeMode(QListWidget::Adjust);
    d->tagGrid->setMovement(QListWidget::Static);
    d->tagGrid->setSpacing(4);
    d->tagGrid->setWordWrap(true);
    d->tagGrid->setUniformItemSizes(true);
    d->tagGrid->setMinimumHeight(104 + 16);
    d->tagGrid->setVisible(false);
    d->previewList      = new QListWidget;
    d->previewList->setViewMode(QListWidget::IconMode);
    // Display only: Download always takes the whole album or
    // tag, so the preview must not offer a selection that
    // suggests individual pictures can be picked.
    d->previewList->setSelectionMode(QAbstractItemView::NoSelection);
    d->previewList->setFocusPolicy(Qt::NoFocus);
    d->previewList->setIconSize(QSize(96, 96));
    // Fixed cell size: items are created before their thumbnails
    // stream in, and without a grid size the widget sizes cells
    // from the (still empty) icons, leaving the late-arriving
    // thumbnails squashed until a rebuild relayouts them.
    d->previewList->setGridSize(QSize(116, 128));
    d->previewList->setMinimumHeight(128 + 16);
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
    d->newAlbumBtn->setVisible(d->mode == Mode::Export);
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

    // The settings actions sit with the settings they belong to,
    // at natural width, instead of stretching across the dialog.
    auto* const settingsBtnRow = new QHBoxLayout;
    settingsBtnRow->addWidget(saveBtn);
    settingsBtnRow->addWidget(d->refreshBtn);
    settingsBtnRow->addWidget(d->newAlbumBtn);
    settingsBtnRow->addStretch();
    layout->addLayout(settingsBtnRow);

    // A rule between the server settings and the working view.
    auto* const separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    layout->addWidget(d->filterEdit);

    if (d->mode == Mode::Import)
    {
        layout->addLayout(sourceRow);
    }

    layout->addWidget(d->albumGrid, 2);
    layout->addWidget(d->tagGrid, 2);

    if (d->mode == Mode::Import)
    {
        layout->addLayout(previewRow);
        layout->addWidget(d->previewList, 3);
    }
    else
    {
        // The export tool's preview is the upload set itself:
        // the images currently selected in digiKam.
        layout->addWidget(new QLabel(QStringLiteral("Images selected in digiKam:")));
        layout->addWidget(d->previewList, 3);
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
    connect(d->albumGrid, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item)
            {
                d->targetLabel->setText(item
                    ? QStringLiteral("Upload target: %1").arg(item->data(Qt::UserRole + 1).toString())
                    : QStringLiteral("Upload target: no album (library only)"));
                refreshTileStyles();
                updateExportStatus();

                if (item && d->mode == Mode::Import)
                {
                    startPreview(item->data(Qt::UserRole).toString());
                }
            });
    connect(d->albumGrid, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item)
            {
                if (item && d->mode == Mode::Import)
                {
                    startPreview(item->data(Qt::UserRole).toString());
                }
            });
    connect(d->tagGrid, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item)
            {
                if (item && d->mode == Mode::Import)
                {
                    startPreview(item->data(Qt::UserRole).toString());
                }
            });
    // A click on the already-current tile retries the preview;
    // currentItemChanged alone would not fire again after an
    // error left the tile selected with no contents shown.
    connect(d->tagGrid, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item)
            {
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
    connect(d->talker, &ImmichTalker::signalSyncRecord, this, &ImmichWindow::slotSyncRecord);
    connect(d->talker, &ImmichTalker::signalAssetPatched, this, &ImmichWindow::slotAssetPatched);
    connect(d->talker, &ImmichTalker::signalTagCreated, this, &ImmichWindow::slotTagCreated);
    connect(d->talker, &ImmichTalker::signalBulkTagged, this, &ImmichWindow::slotBulkTagged);
    connect(d->talker, &ImmichTalker::signalIdentitySearch, this, &ImmichWindow::slotIdentitySearch);
    connect(d->talker, &ImmichTalker::signalAlbumsForAsset, this, &ImmichWindow::slotRestoreAlbums);
    connect(d->talker, &ImmichTalker::signalAssetsDeleted, this, &ImmichWindow::slotAssetsDeleted);
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

void ImmichWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    if (d->mode == Mode::Export)
    {
        // The digiKam selection may have changed since the tool
        // was last open; it is the upload set, so show it afresh.
        fillUploadPreview();
        updateExportStatus();
    }

    // With credentials already stored, opening the tool is
    // intent enough: refresh the sources without making the
    // user press Refresh first.
    if (!d->session->url().isEmpty() && !d->session->apiKey().isEmpty()
        && !d->uploading && !d->downloading)
    {
        slotRefreshAlbums();
    }
}

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

    for (const QUrl& url : d->iface->currentSelectedItems())
    {
        const QString path = url.toLocalFile();
        auto* const item = new QListWidgetItem(QFileInfo(path).fileName(), d->previewList);
        item->setSizeHint(QSize(116, 128));
        item->setToolTip(path);

        QImageReader reader(path);
        reader.setScaledSize(QSize(96, 96));
        const QImage image = reader.read();
        QPixmap pix = image.isNull() ? QPixmap() : QPixmap::fromImage(image);

        if (pix.width() > 96 || pix.height() > 96)
        {
            pix = pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        item->setIcon(pix.isNull() ? placeholder : QIcon(pix));
    }
}

void ImmichWindow::updateExportStatus()
{
    if (d->mode != Mode::Export || d->uploading)
    {
        return;
    }

    const int count = d->iface ? d->iface->currentSelectedItems().size() : 0;

    if (count == 0)
    {
        d->status->setText(QStringLiteral("No images selected in digiKam."));
        return;
    }

    const QString target = currentSourceName();
    QString text = QStringLiteral("%1 image%2 selected will be uploaded")
                       .arg(count)
                       .arg(count == 1 ? QString() : QStringLiteral("s"));
    text += target.isEmpty() ? QStringLiteral(" to the library.")
                             : QStringLiteral(" to '%1'.").arg(target);
    d->status->setText(text);
}

void ImmichWindow::slotSaveSettings()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->session->save();
    d->status->setText(QStringLiteral("Settings saved."));
}

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
    QListWidgetItem* const item = d->sourceIsTags ? d->tagGrid->currentItem()
                                                  : d->albumGrid->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString ImmichWindow::currentSourceName() const
{
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
        auto* const countLabel = new QLabel(QStringLiteral("%1 items").arg(countText));
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

void ImmichWindow::slotRefreshAlbums()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->listingCache.clear();

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
    d->albumGrid->setVisible(!tags);
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
        d->status->setText(QStringLiteral("%1 albums").arg(albums.size()));
    }
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
    d->identityStrict = false;
    d->identityDimsDiffer = false;
    d->identityWaitingThumb = false;
    d->matchedSync   = 0;
    d->matchedMulti  = 0;
    d->replacedCount = 0;
    d->replaceOldByPath.clear();
    d->replaces.clear();
    d->restoreQueue.clear();
    d->restoreAlbums.clear();
    d->restoring = false;

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
    const QString path = d->identityUrl.toLocalFile();
    d->status->setText(QStringLiteral("Checking for an existing copy of %1...")
                           .arg(QFileInfo(path).fileName()));
    d->talker->searchByFileName(QFileInfo(path).fileName());
}

void ImmichWindow::slotIdentitySearch(const QString& /*name*/, const QJsonArray& items)
{
    if (d->cancelling || !d->uploading)
    {
        return;
    }

    const QUrl    url  = d->identityUrl;
    const QString path = url.toLocalFile();
    QList<IdentityCandidate> candidates;

    for (const QJsonValue& v : items)
    {
        const QJsonObject o = v.toObject();
        IdentityCandidate c;
        c.id            = o.value(QStringLiteral("id")).toString();
        c.name          = o.value(QStringLiteral("originalFileName")).toString();
        c.localDateTime = o.value(QStringLiteral("localDateTime")).toString();
        c.createdAt     = o.value(QStringLiteral("createdAt")).toString();
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

    const IdentityMatch match = chooseIdentityMatch(candidates, QFileInfo(path).fileName(),
                                                    dateStr, width, height);

    if (match.assetId.isEmpty())
    {
        d->pending.append(url);
        nextIdentity();
        return;
    }

    if (match.candidates > 1)
    {
        ++d->matchedMulti;
    }

    d->identityCandidateId  = match.assetId;
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

    const QUrl    url  = d->identityUrl;
    const QString path = url.toLocalFile();

    QImage localImage;
    QImageReader reader(path);
    reader.setAutoTransform(true);   // match the server thumbnail's orientation

    if (reader.size().isValid())
    {
        QSize scaled = reader.size();
        scaled.scale(320, 320, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }

    localImage = reader.read();

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
            d->replaceOldByPath.insert(path, d->identityCandidateId);
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
        // copy, sync digiKam's metadata onto it.
        d->uploadedIds.append(d->identityCandidateId);
        Private::SyncJob job;
        job.assetId   = d->identityCandidateId;
        job.localPath = path;
        job.dk        = dkFieldsFor(url);
        d->syncJobs.append(job);
        ++d->matchedSync;
        ++d->alreadyThere;
        ++d->done;
        d->progress->setValue(d->done);
        nextIdentity();
        return;
    }

    if (d->identityStrict)
    {
        // A real edit of a photo the server holds: upload it and
        // replace the old copy once the upload lands.
        d->replaceOldByPath.insert(path, d->identityCandidateId);
    }

    d->pending.append(url);
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

    d->status->setText(QStringLiteral("Uploading %1 (%2 of %3)")
                           .arg(info.fileName())
                           .arg(d->done + 1)
                           .arg(d->total));

    d->currentUploadFields = dkFieldsFor(url);
    d->haveUploadFields    = true;
    d->currentUploadPath   = path;
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
    d->identityQueue.clear();
    d->identityWaitingThumb = false;
    d->restoring = false;
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
        d->listingCache.remove(QStringLiteral("album:") + d->targetAlbumId);
    }

    d->albumAssigned = true;
    startRestoreOrSync();
}

void ImmichWindow::startRestoreOrSync()
{
    if (d->replaces.isEmpty())
    {
        maybeStartSync();
        return;
    }

    // Replaced photos: put each new copy into every album its
    // predecessor belonged to, then trash the predecessors. Only
    // when all of that has landed does the metadata sync start.
    d->restoring    = true;
    d->restoreQueue = d->replaces;
    d->status->setText(QStringLiteral("Replacing %1 edited photo(s) in Immich...")
                           .arg(d->restoreQueue.size()));
    restoreNext();
}

void ImmichWindow::restoreNext()
{
    if (d->restoreQueue.isEmpty())
    {
        QStringList oldIds;

        for (const Private::ReplaceCtx& ctx : d->replaces)
        {
            oldIds.append(ctx.oldId);
        }

        if (!oldIds.isEmpty())
        {
            d->talker->deleteAssets(oldIds);
            return;
        }

        d->restoring = false;
        maybeStartSync();
        return;
    }

    d->talker->listAlbumsForAsset(d->restoreQueue.constFirst().oldId);
}

void ImmichWindow::slotRestoreAlbums(const QString& assetId, const QStringList& albumIds)
{
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
        d->restoreQueue.removeFirst();
        restoreNext();
        return;
    }

    const QString albumId = d->restoreAlbums.takeFirst();
    d->talker->addToAlbum(albumId, { d->restoreQueue.constFirst().newId });
}

void ImmichWindow::slotAssetsDeleted(int httpStatus)
{
    d->restoring = false;

    if (httpStatus >= 200 && httpStatus < 300)
    {
        d->replacedCount = d->replaces.size();

        for (const Private::ReplaceCtx& ctx : d->replaces)
        {
            d->syncState.remove(ctx.oldId);
        }

        saveSyncState(d->syncStatePath, d->syncState);
    }

    d->replaces.clear();
    d->restoreQueue.clear();
    maybeStartSync();
}

void ImmichWindow::printUploadSummary()
{
    QString text = d->albumAssigned
        ? QStringLiteral("Uploaded %1 of %2 items (%3 already on the server, %4 failed) and added them to the album.")
        : QStringLiteral("Uploaded %1 of %2 items (%3 already on the server, %4 failed).");

    text = text.arg(d->done - d->failed - d->alreadyThere)
               .arg(d->total)
               .arg(d->alreadyThere)
               .arg(d->failed);

    if (d->matchedSync > 0)
    {
        text += QStringLiteral(" %1 item(s) matched photos already in Immich;"
                               " their metadata was synced instead of re-uploading.")
                    .arg(d->matchedSync);

        if (d->matchedMulti > 0)
        {
            text += QStringLiteral(" (%1 had several copies on the server; the newest was used.)")
                        .arg(d->matchedMulti);
        }
    }

    if (d->replacedCount > 0)
    {
        text += QStringLiteral(" %1 edited photo(s) replaced their Immich copy"
                               " (the old copies are in Immich's trash).")
                    .arg(d->replacedCount);
    }

    if (d->syncRan)
    {
        text += QStringLiteral(" Metadata synced for %1 already-uploaded items (%2 unchanged, %3 failed).")
                    .arg(d->syncUpdated)
                    .arg(d->syncClean)
                    .arg(d->syncFailed);

        if (d->syncConflicts > 0)
        {
            text += QStringLiteral(" %1 conflict(s) settled by timestamp.")
                        .arg(d->syncConflicts);
        }

        if (d->syncPermsDenied)
        {
            text += QStringLiteral(" Metadata sync was refused: the API key also needs"
                                   " asset.update, tag.create and tag.asset.");
        }
    }

    d->status->setText(text);
}

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
    d->status->setText(QStringLiteral("Syncing metadata for %1 already-uploaded items...")
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

    for (const Private::SyncJob& candidate : d->syncJobs)
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

    for (const QString& tag : plan.tagsToAdd)
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

    for (const Private::SyncJob& candidate : d->syncJobs)
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
    d->status->setText(QStringLiteral("Syncing tags..."));
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
        this, QStringLiteral("Download to"), startDir);

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
    d->listingKey     = (d->sourceIsTags ? QStringLiteral("tag:")
                                         : QStringLiteral("album:")) + sourceId;
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
    if (d->uploading || d->downloading || sourceId.isEmpty())
    {
        return;
    }

    const QString key = (d->sourceIsTags ? QStringLiteral("tag:")
                                         : QStringLiteral("album:")) + sourceId;
    const auto cached = d->listingCache.constFind(key);

    if (cached != d->listingCache.constEnd())
    {
        d->previewSourceId = sourceId;
        d->previewAssets   = cached.value();
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

void ImmichWindow::slotAlbumAssets(const QList<ImmichAssetRef>& assets)
{
    if (d->listingPurpose == Private::ListingPurpose::Preview)
    {
        d->listingPurpose = Private::ListingPurpose::None;
        d->previewAssets  = assets;
        d->listingCache.insert(d->listingKey, assets);
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
    d->listingCache.insert(d->listingKey, assets);
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

    // Post-pull snapshot: digiKam now holds the server's fields.
    const ServerFields sv = serverFieldsFromRecord(record);
    storeSnapshot(assetId, sv.fields, sv.updatedAt, sv.checksum, sv.fields);

    slotDownloadFinished(assetId, true);
}

void ImmichWindow::slotMetaRecord(const QString& assetId, const QString& targetFile,
                                  const QJsonObject& record)
{
    const bool updated = writeRecordSidecar(targetFile, record);

    const ServerFields sv = serverFieldsFromRecord(record);
    storeSnapshot(assetId, sv.fields, sv.updatedAt, sv.checksum, sv.fields);

    slotMetaFinished(assetId, updated);
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
        const QString   packet = buildSidecarPacket(tags, rating, description, hasGps,
                                                    lat.toDouble(), lon.toDouble());
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

    if (d->tagsPurpose == Private::TagsPurpose::Sync)
    {
        // The sync's tag list could not be fetched: treat it as
        // empty so the sync chain completes (creations will be
        // attempted and counted) instead of stalling.
        d->tagsPurpose = Private::TagsPurpose::Ui;
        handleSyncTagList(QList<ImmichTag>());
        return;
    }

    if (d->listingPurpose == Private::ListingPurpose::Preview)
    {
        // A failed preview must not wedge the listing machinery:
        // startPreview refuses to run while a purpose is set, so
        // leaving it here blocked every later selection.
        d->listingPurpose = Private::ListingPurpose::None;
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
