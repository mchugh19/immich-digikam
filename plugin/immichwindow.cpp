/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
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

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

// Local includes

#include "dinfointerface.h"
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

    QLineEdit*   urlEdit    = nullptr;
    QLineEdit*   keyEdit    = nullptr;
    QListWidget* albumList  = nullptr;
    QLabel*      status     = nullptr;
    QPushButton* refreshBtn = nullptr;
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
    d->albumList        = new QListWidget;
    d->status           = new QLabel;

    auto* const layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(saveBtn);
    layout->addWidget(d->refreshBtn);
    layout->addWidget(d->albumList);
    layout->addWidget(d->status);

    connect(saveBtn, &QPushButton::clicked, this, &ImmichWindow::slotSaveSettings);
    connect(d->refreshBtn, &QPushButton::clicked, this, &ImmichWindow::slotRefreshAlbums);
    connect(d->talker, &ImmichTalker::signalAlbums, this, &ImmichWindow::slotAlbums);
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

void ImmichWindow::slotError(const QString& msg)
{
    d->status->setText(msg);
}

void ImmichWindow::slotBusy(bool val)
{
    d->refreshBtn->setEnabled(!val);
}

} // namespace DigikamGenericImmichPlugin
