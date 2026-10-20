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

#ifndef DIGIKAM_IMMICH_WINDOW_P_H
#define DIGIKAM_IMMICH_WINDOW_P_H

// Qt includes

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPainter>
#include <QPair>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWidget>

// Local includes

#include "immichitem.h"
#include "immichsync.h"
#include "immichwindow.h"

class KUrlRequester;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QStackedLayout;

namespace DigikamGenericImmichPlugin
{

// A small throbber drawn with QPainter. digiKam has an
// equivalent widget (WorkingWidget), but it is internal to
// libdigikamcore: its header is not among the installed
// digiKam headers, so the plugin draws its own.
class SpinnerWidget : public QWidget
{
    Q_OBJECT

public:

    explicit SpinnerWidget(QWidget* const parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(32, 32);
        m_timer.setInterval(100);

        connect(&m_timer, &QTimer::timeout,
                this, [this]()
                {
                    m_step = (m_step + 1) % 12;
                    update();
                });
    }

    void toggleTimer(bool turnOn)
    {
        if (turnOn)
        {
            m_timer.start();
        }
        else
        {
            m_timer.stop();
        }
    }

protected:

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width() / 2.0, height() / 2.0);

        const qreal  radius = qMin(width(), height()) / 2.0 - 2.0;
        const QColor base   = palette().color(QPalette::Text);
        painter.setPen(Qt::NoPen);

        for (int i = 0; i < 12; ++i)
        {
            // Spokes fade behind the leading one, which advances
            // one position per timer tick.
            const int behind = (12 + m_step - i) % 12;
            QColor spoke = base;
            spoke.setAlpha(255 - behind * 18);
            painter.setBrush(spoke);
            painter.save();
            painter.rotate(i * 30.0);
            painter.drawRoundedRect(QRectF(radius * 0.5, -1.5, radius * 0.5, 3.0), 1.5, 1.5);
            painter.restore();
        }
    }

private:

    QTimer m_timer;
    int    m_step = 0;
};

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
    QWidget*      albumArea   = nullptr;
    QStackedLayout* albumStack = nullptr;
    QWidget*      albumLoadingOverlay = nullptr;
    SpinnerWidget* albumSpinner = nullptr;
    bool          albumsLoading = false;
    QListWidget*  tagGrid     = nullptr;
    QListWidget*  previewList = nullptr;
    QComboBox*    previewSort = nullptr;
    QLabel*       targetLabel = nullptr;
    QLabel*       status      = nullptr;
    QPushButton*  refreshBtn  = nullptr;
    QPushButton*  newAlbumBtn = nullptr;
    KUrlRequester* downloadDirEdit = nullptr;
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
    // Contents listings are cached in the session (see
    // ImmichSession), so they survive this window. Thumbnails
    // are cached on disk instead (thumbCacheDir), across
    // sessions.
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

    // Thumbnails of LOCAL files (the selection preview in the
    // export window, the identity check's local frame) come from
    // digiKam's own thumbnail machinery, which decodes every
    // format digiKam supports, HEIF included; QImageReader has
    // no HEIF reader. The thread is the window's child and is
    // configured to deliver the bare thumbnail (no highlight
    // frame) and a null pixmap when a load fails, never a
    // surrogate system icon, so a failure stays distinguishable
    // from a real thumbnail. Server asset thumbnails keep the
    // download pipeline above: that data is remote, digiKam has
    // no file for it.
    ThumbnailLoadThread* localThumbThread = nullptr;

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
    QString      identityStackId;
    bool         identityStrict = false;
    bool         identityDimsDiffer = false;
    bool         identityWaitingThumb = false;
    // The server thumbnail's bytes, held while the matching
    // local frame is loaded by the thumbnail thread.
    QByteArray   identityThumbBytes;
    bool         identityWaitingLocal = false;
    int          identityPass = 1;      // 1 = target-album scope, 2 = library
    bool         identityNameSeen = false;
    int          matchedSync   = 0;
    int          matchedMulti  = 0;
    int          stackedCount  = 0;
    int          trashFallbackCount = 0;
    QHash<QString, QString> replaceOldByPath;
    QHash<QString, QString> replaceStackByPath;
    struct ReplaceCtx
    {
        QString oldId;
        QString newId;
        QString localPath;
        QString stackId;   // the old copy's stack, if it has one
    };
    QList<ReplaceCtx> replaces;
    QList<ReplaceCtx> restoreQueue;
    QStringList  restoreAlbums;
    QStringList  restoreTrashQueue;   // stacking refused: fall back to trash
    QStringList  overwriteQueue;      // user chose overwrite: trash by choice
    int          overwriteCount = 0;
    bool         replaceByStack = true;
    QComboBox*   replaceCombo = nullptr;
    QRadioButton* libraryRadio = nullptr;
    QRadioButton* albumRadio   = nullptr;
    QWidget*     pickerBox     = nullptr;
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

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_WINDOW_P_H
