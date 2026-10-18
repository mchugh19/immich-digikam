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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

// Local includes

#include "dinfointerface.h"
#include "dmetadata.h"
#include "immichsession.h"
#include "immichtalker.h"

namespace DigikamGenericImmichPlugin
{

class ImmichWindow::Private
{
public:

    DInfoInterface* iface   = nullptr;
    ImmichSession*  session = nullptr;
    ImmichTalker*   talker  = nullptr;

    QLineEdit*    urlEdit     = nullptr;
    QLineEdit*    keyEdit     = nullptr;
    QListWidget*  albumList   = nullptr;
    QLabel*       status      = nullptr;
    QPushButton*  refreshBtn  = nullptr;
    QPushButton*  uploadBtn   = nullptr;
    QProgressBar* progress    = nullptr;

    // Upload queue state.
    QList<QUrl>  pending;
    QStringList  uploadedIds;
    QString      targetAlbumId;
    QString      tempSidecar;
    int          total       = 0;
    int          done        = 0;
    int          failed      = 0;
    bool         uploading   = false;
};

ImmichWindow::ImmichWindow(DInfoInterface* const iface, QWidget* const parent)
    : QDialog(parent),
      d(new Private)
{
    d->iface   = iface;
    d->session = new ImmichSession;
    d->session->load();
    d->talker  = new ImmichTalker(d->session, this);

    setWindowTitle(QStringLiteral("Immich"));

    auto* const form = new QFormLayout;
    d->urlEdit = new QLineEdit(d->session->url());
    d->urlEdit->setPlaceholderText(QStringLiteral("https://photos.example.com"));
    d->keyEdit = new QLineEdit(d->session->apiKey());
    d->keyEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Server URL:"), d->urlEdit);
    form->addRow(QStringLiteral("API key:"), d->keyEdit);

    auto* const saveBtn = new QPushButton(QStringLiteral("Save settings"));
    d->refreshBtn       = new QPushButton(QStringLiteral("Refresh albums"));
    d->uploadBtn        = new QPushButton(QStringLiteral("Upload selection"));
    d->albumList        = new QListWidget;
    d->progress         = new QProgressBar;
    d->progress->setVisible(false);
    d->status           = new QLabel;

    auto* const layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(saveBtn);
    layout->addWidget(d->refreshBtn);
    layout->addWidget(d->albumList);
    layout->addWidget(d->uploadBtn);
    layout->addWidget(d->progress);
    layout->addWidget(d->status);

    connect(saveBtn, &QPushButton::clicked, this, &ImmichWindow::slotSaveSettings);
    connect(d->refreshBtn, &QPushButton::clicked, this, &ImmichWindow::slotRefreshAlbums);
    connect(d->uploadBtn, &QPushButton::clicked, this, &ImmichWindow::slotUploadSelection);
    connect(d->talker, &ImmichTalker::signalAlbums, this, &ImmichWindow::slotAlbums);
    connect(d->talker, &ImmichTalker::signalAlbumUpdated, this, &ImmichWindow::slotAlbumUpdated);
    connect(d->talker, &ImmichTalker::signalUploadSucceeded, this, &ImmichWindow::slotUploadSucceeded);
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

void ImmichWindow::slotRefreshAlbums()
{
    d->session->setUrl(d->urlEdit->text());
    d->session->setApiKey(d->keyEdit->text());
    d->talker->listAlbums();
}

void ImmichWindow::slotAlbums(const QList<ImmichAlbum>& albums)
{
    d->albumList->clear();

    for (const ImmichAlbum& album : albums)
    {
        auto* const item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(album.name).arg(album.assetCount), d->albumList);
        item->setData(Qt::UserRole, album.id);
    }

    d->status->setText(QStringLiteral("%1 albums").arg(albums.size()));
}

void ImmichWindow::slotUploadSelection()
{
    if (d->uploading)
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

    QListWidgetItem* const albumItem = d->albumList->currentItem();
    d->targetAlbumId = albumItem ? albumItem->data(Qt::UserRole).toString()
                                 : QString();

    d->pending     = items;
    d->uploadedIds.clear();
    d->total       = items.size();
    d->done        = 0;
    d->failed      = 0;
    d->uploading   = true;

    d->refreshBtn->setEnabled(false);
    d->uploadBtn->setEnabled(false);
    d->progress->setRange(0, d->total);
    d->progress->setValue(0);
    d->progress->setVisible(true);

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

    d->talker->uploadAsset(path, sidecarFor(path));
}

void ImmichWindow::slotUploadSucceeded(const QString& assetId, const QString& /*status*/)
{
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
    d->refreshBtn->setEnabled(true);
    d->uploadBtn->setEnabled(true);

    if (!d->tempSidecar.isEmpty())
    {
        QFile::remove(d->tempSidecar);
        d->tempSidecar.clear();
    }

    if (!d->targetAlbumId.isEmpty() && !d->uploadedIds.isEmpty())
    {
        d->status->setText(QStringLiteral("Adding %1 items to the album...")
                               .arg(d->uploadedIds.size()));
        d->talker->addToAlbum(d->targetAlbumId, d->uploadedIds);
        return;
    }

    d->status->setText(QStringLiteral("Uploaded %1 of %2 items (%3 failed).")
                           .arg(d->done - d->failed)
                           .arg(d->total)
                           .arg(d->failed));
}

void ImmichWindow::slotAlbumUpdated()
{
    d->status->setText(QStringLiteral("Uploaded %1 of %2 items (%3 failed) and added them to the album.")
                           .arg(d->done - d->failed)
                           .arg(d->total)
                           .arg(d->failed));
}

void ImmichWindow::slotError(const QString& msg)
{
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
    if (!d->uploading)
    {
        d->refreshBtn->setEnabled(!val);
    }
}

QString ImmichWindow::sidecarFor(const QString& filePath)
{
    const QFileInfo info(filePath);

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

    // No on-disk sidecar: fall back to the file's embedded XMP
    // packet, written out as a sidecar so Immich gives it the
    // sidecar precedence it applies to uploaded metadata.
    DMetadata meta;

    if (meta.load(filePath))
    {
        const QByteArray xmp = meta.getXmp();

        if (!xmp.isEmpty())
        {
            const QString tempPath = QDir::tempPath()
                                     + QStringLiteral("/immich-digikam-")
                                     + info.fileName()
                                     + QStringLiteral(".xmp");
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
