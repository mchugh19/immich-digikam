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
#include "thumbnailloadthread.h"
#include "thumbnailinfo.h"
#include "immichxmp.h"

namespace DigikamGenericImmichPlugin
{

ImmichWindow::ImmichWindow(DInfoInterface* const iface,
                           ImmichSession* const session, Mode mode,
                           QWidget* const parent)
    : QDialog(parent),
      d(new Private)
{
    d->iface   = iface;
    d->mode    = mode;
    // The session is owned by the plugin and outlives this
    // window; the state that must survive the window (settings,
    // the listing cache) lives there, not here.
    d->session = session;
    d->talker  = new ImmichTalker(d->session, this);

    // Local-file thumbnails are loaded by digiKam's own
    // thumbnail thread (see Private::localThumbThread). A
    // dedicated instance, not the shared default thread: the
    // configuration below is this window's business and must
    // not leak into digiKam's own views.
    d->localThumbThread = new ThumbnailLoadThread(this);
    d->localThumbThread->setHighlightPixmap(false);
    d->localThumbThread->setSendSurrogatePixmap(false);
    connect(d->localThumbThread, &ThumbnailLoadThread::signalThumbnailLoaded,
            this, &ImmichWindow::slotLocalThumbnailLoaded);

    // This window destroys itself when closed. It must not be
    // owned by the plugin: deleting widgets from the plugin
    // destructor runs while the plugin library is being
    // unloaded at process exit, when Qt's GUI infrastructure
    // (the shortcut map a mnemonic label unregisters from) is
    // already gone, which crashes at exit.
    setAttribute(Qt::WA_DeleteOnClose);

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

    setWindowTitle(d->mode == Mode::Export ? i18n("Export to Immich")
                                           : i18n("Import from Immich"));

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
    form->addRow(i18n("Server URL:"), d->urlEdit);
    form->addRow(i18n("API key:"), d->keyEdit);
    // What happens to the previous copy when a photo edited in
    // digiKam is exported again: stack the new version over it
    // (nothing is deleted) or overwrite it (old copy to trash).
    // Metadata-only changes never reach this choice: they sync.
    d->replaceCombo = new QComboBox;
    d->replaceCombo->addItem(i18n("Stack the new version over the old"),
                             QStringLiteral("stack"));
    d->replaceCombo->addItem(i18n("Overwrite (old copy to Immich's trash)"),
                             QStringLiteral("trash"));
    d->replaceCombo->setCurrentIndex(
        d->session->replaceMode() == QStringLiteral("trash") ? 1 : 0);
    form->addRow(i18n("Edited photos:"), d->replaceCombo);

    auto* const saveBtn = new QPushButton(i18n("Save settings"));
    d->refreshBtn       = new QPushButton(i18n("Refresh albums"));
    d->newAlbumBtn      = new QPushButton(i18n("New album..."));
    d->uploadBtn        = new QPushButton(i18n("Upload selection"));
    d->downloadBtn      = new QPushButton(i18n("Download"));
    d->cancelBtn        = new QPushButton(i18n("Cancel transfer"));
    d->cancelBtn->setEnabled(false);

    // Standard theme icons, the ones digiKam's own dialogs use
    // for the same actions, so the window sits naturally in the
    // host's icon theme.
    saveBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    d->refreshBtn->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    d->newAlbumBtn->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
    d->uploadBtn->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    d->downloadBtn->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    d->cancelBtn->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
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

    // The album area carries the grid plus a loading overlay
    // (a throbber in a small panel over the grid): while the
    // album list is in flight the overlay shows over the grid,
    // so a slow server reads as "loading" rather than as an
    // empty or stale album view.
    d->albumArea = new QWidget;
    d->albumArea->setMinimumHeight(248 + 16);
    d->albumStack = new QStackedLayout(d->albumArea);
    d->albumStack->setStackingMode(QStackedLayout::StackAll);
    d->albumStack->addWidget(d->albumGrid);
    d->albumLoadingOverlay = new QWidget(d->albumArea);

    auto* const overlayLayout = new QVBoxLayout(d->albumLoadingOverlay);
    overlayLayout->addStretch();

    auto* const loadingCard = new QFrame(d->albumLoadingOverlay);
    loadingCard->setFrameShape(QFrame::StyledPanel);
    loadingCard->setAutoFillBackground(true);
    auto* const cardLayout = new QVBoxLayout(loadingCard);
    d->albumSpinner = new SpinnerWidget(loadingCard);
    cardLayout->addWidget(d->albumSpinner, 0, Qt::AlignHCenter);
    auto* const loadingLabel = new QLabel(i18n("Loading albums..."), loadingCard);
    loadingLabel->setAlignment(Qt::AlignHCenter);
    cardLayout->addWidget(loadingLabel);

    auto* const cardRow = new QHBoxLayout;
    cardRow->addStretch();
    cardRow->addWidget(loadingCard);
    cardRow->addStretch();
    overlayLayout->addLayout(cardRow);
    overlayLayout->addStretch();

    d->albumStack->addWidget(d->albumLoadingOverlay);
    d->albumStack->setCurrentWidget(d->albumGrid);
    d->albumLoadingOverlay->setVisible(false);
    d->albumSpinner->toggleTimer(false);

    d->filterEdit       = new QLineEdit;
    d->filterEdit->setPlaceholderText(i18n("Filter..."));
    d->filterEdit->setClearButtonEnabled(true);
    d->albumsSrc        = new QRadioButton(i18n("Albums"));
    d->tagsSrc          = new QRadioButton(i18n("Tags"));
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
    d->previewSort->addItems({ i18n("Date (oldest first)"),
                               i18n("Date (newest first)"),
                               i18n("Name") });
    d->targetLabel      = new QLabel(i18n("Upload target: no album (library only)"));
    d->libraryRadio     = new QRadioButton(i18n("Library only"));
    d->albumRadio       = new QRadioButton(i18n("Album"));
    d->albumRadio->setChecked(d->session->exportTargetMode() == QStringLiteral("album"));
    d->libraryRadio->setChecked(!d->albumRadio->isChecked());
    d->progress         = new QProgressBar;
    d->progress->setFixedHeight(18);
    d->status           = new QLabel;

    // The download target lives in the window as a KDE URL
    // requester (path field plus folder button), remembered
    // between runs, instead of a modal folder dialog appearing
    // every time Download is pressed.
    d->downloadDirEdit = new KUrlRequester(this);
    d->downloadDirEdit->setMode(KFile::Directory | KFile::LocalOnly | KFile::ExistingOnly);
    d->downloadDirEdit->setUrl(QUrl::fromLocalFile(
        d->session->downloadDir().isEmpty() ? QDir::homePath()
                                            : d->session->downloadDir()));

    // Each tool shows only its own direction's actions. The source
    // switch and the contents preview belong to the import tool;
    // the filter serves the source list in both.
    d->targetLabel->setVisible(d->mode == Mode::Export);
    d->replaceCombo->setVisible(d->mode == Mode::Export);
    d->uploadBtn->setVisible(d->mode == Mode::Export);
    d->newAlbumBtn->setVisible(d->mode == Mode::Export);
    d->downloadBtn->setVisible(d->mode == Mode::Import);

    auto* const sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(i18n("Import from:")));
    sourceRow->addWidget(d->albumsSrc);
    sourceRow->addWidget(d->tagsSrc);
    sourceRow->addStretch();

    auto* const previewRow = new QHBoxLayout;
    previewRow->addWidget(new QLabel(i18n("Contents:")));
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

    if (d->mode == Mode::Export)
    {
        // Destination choice: the choice is mutually exclusive,
        // so it is a radio group with a caption (the KDE HIG's
        // shape for this), and the album picker is subordinate
        // to the Album radio, indented beneath it. The picker is
        // hidden entirely while Library only is chosen, so a
        // library export gives the whole window height to the
        // selection preview.
        auto* const targetGroup = new QVBoxLayout;
        targetGroup->addWidget(new QLabel(i18n("Upload to")));

        auto* const targetRadios = new QVBoxLayout;
        targetRadios->setContentsMargins(24, 0, 0, 0);
        targetRadios->addWidget(d->libraryRadio);
        targetRadios->addWidget(d->albumRadio);
        targetGroup->addLayout(targetRadios);
        layout->addLayout(targetGroup);

        d->pickerBox = new QWidget;
        auto* const pickerLayout = new QVBoxLayout(d->pickerBox);
        pickerLayout->setContentsMargins(48, 0, 0, 0);
        pickerLayout->addWidget(d->filterEdit);
        pickerLayout->addWidget(d->albumArea, 2);
        layout->addWidget(d->pickerBox, 2);
        layout->addWidget(d->tagGrid, 2);
    }
    else
    {
        layout->addWidget(d->filterEdit);
        layout->addLayout(sourceRow);
        layout->addWidget(d->albumArea, 2);
        layout->addWidget(d->tagGrid, 2);
    }

    if (d->mode == Mode::Import)
    {
        layout->addLayout(previewRow);
        layout->addWidget(d->previewList, 3);
    }
    else
    {
        // The export tool's preview is the upload set itself:
        // the images currently selected in digiKam.
        layout->addWidget(new QLabel(i18n("Images selected in digiKam:")));
        layout->addWidget(d->previewList, 3);
    }

    layout->addWidget(d->targetLabel);
    layout->addWidget(d->uploadBtn);

    if (d->mode == Mode::Import)
    {
        auto* const downloadDirRow = new QHBoxLayout;
        downloadDirRow->addWidget(new QLabel(i18n("Download to:")));
        downloadDirRow->addWidget(d->downloadDirEdit, 1);
        layout->addLayout(downloadDirRow);
    }

    layout->addWidget(d->downloadBtn);

    // The progress bar and Cancel share one always-present
    // row: the bar stays visible (empty when idle) and Cancel
    // is enabled only during a transfer, so starting or
    // finishing a transfer never moves anything in the window.
    auto* const progressRow = new QHBoxLayout;
    progressRow->addWidget(d->progress, 1);
    progressRow->addWidget(d->cancelBtn);
    layout->addLayout(progressRow);
    layout->addWidget(d->status);

    connect(saveBtn, &QPushButton::clicked, this, &ImmichWindow::slotSaveSettings);
    connect(d->refreshBtn, &QPushButton::clicked, this, &ImmichWindow::slotRefreshAlbums);
    connect(d->newAlbumBtn, &QPushButton::clicked, this, &ImmichWindow::slotNewAlbum);
    connect(d->talker, &ImmichTalker::signalAlbumCreated, this, &ImmichWindow::slotAlbumCreated);
    connect(d->uploadBtn, &QPushButton::clicked, this, &ImmichWindow::slotUploadSelection);
    connect(d->albumRadio, &QRadioButton::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    d->session->setExportTargetMode(QStringLiteral("album"));
                    d->session->save();
                }

                applyTargetMode();
            });
    connect(d->libraryRadio, &QRadioButton::toggled, this,
            [this](bool on)
            {
                if (on)
                {
                    d->session->setExportTargetMode(QStringLiteral("library"));
                    d->session->save();
                }

                applyTargetMode();
            });
    applyTargetMode();
    connect(d->albumGrid, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item)
            {
                updateTargetLabel();
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
    connect(d->replaceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        d->session->setReplaceMode(d->replaceCombo->itemData(index).toString());
        d->session->save();
    });
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
    connect(d->talker, &ImmichTalker::signalStackCreated, this, &ImmichWindow::slotStackCreated);
    connect(d->talker, &ImmichTalker::signalStackFetched, this, &ImmichWindow::slotStackFetched);
    connect(d->talker, &ImmichTalker::signalError, this, &ImmichWindow::slotError);
    connect(d->talker, &ImmichTalker::signalBusy, this, &ImmichWindow::slotBusy);
}

ImmichWindow::~ImmichWindow()
{
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

void ImmichWindow::updateExportStatus()
{
    if (d->mode != Mode::Export || d->uploading)
    {
        return;
    }

    const int count = d->iface ? d->iface->currentSelectedItems().size() : 0;

    if (count == 0)
    {
        d->status->setText(i18n("No images selected in digiKam."));
        return;
    }

    const QString target = currentSourceName();
    QString text = i18np("%1 image selected will be uploaded",
                         "%1 images selected will be uploaded", count);
    text += target.isEmpty() ? i18n(" to the library.")
                             : i18n(" to '%1'.").arg(target);
    d->status->setText(text);
}

void ImmichWindow::slotSaveSettings()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->session->save();
    d->status->setText(i18n("Settings saved."));
}

void ImmichWindow::applyTargetMode()
{
    if (d->mode != Mode::Export)
    {
        return;
    }

    const bool album = d->albumRadio->isChecked();
    d->pickerBox->setVisible(album);
    d->newAlbumBtn->setVisible(album);
    updateTargetLabel();
    updateExportStatus();
}

void ImmichWindow::updateTargetLabel()
{
    if (d->mode != Mode::Export)
    {
        return;
    }

    const QString name = currentSourceName();
    d->targetLabel->setText(name.isEmpty()
                                ? i18n("Upload target: no album (library only)")
                                : i18n("Upload target: %1").arg(name));
}

void ImmichWindow::setTransferButtonsEnabled(bool enabled)
{
    d->refreshBtn->setEnabled(enabled);
    d->newAlbumBtn->setEnabled(enabled);
    d->uploadBtn->setEnabled(enabled);
    d->downloadBtn->setEnabled(enabled);
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
    d->status->setText(i18n("Cancelling..."));
    d->talker->cancel();
}

void ImmichWindow::slotError(const QString& msg)
{
    if (!d->uploading && !d->downloading)
    {
        // An idle-time error is the album list's failure mode:
        // the load is over either way, so the spinner stops.
        setAlbumLoading(false);
    }

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
        d->status->setText(i18n("Duplicate check failed (%1); uploading everything.").arg(msg));
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
        d->status->setText(i18n("Failed: %1").arg(msg));
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

} // namespace DigikamGenericImmichPlugin
