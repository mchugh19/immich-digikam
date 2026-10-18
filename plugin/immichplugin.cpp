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

#include "immichplugin.h"

// KDE includes

#include <klocalizedstring.h>

// Local includes

#include "immichwindow.h"

namespace DigikamGenericImmichPlugin
{

ImmichPlugin::ImmichPlugin(QObject* const parent)
    : DPluginGeneric(parent)
{
}

ImmichPlugin::~ImmichPlugin()
{
    delete m_toolDlg;
}

QString ImmichPlugin::name() const
{
    return i18n("Immich");
}

QString ImmichPlugin::iid() const
{
    return QLatin1String(DPLUGIN_IID);
}

QIcon ImmichPlugin::icon() const
{
    // TODO: ship a dk-immich icon set under icons/ (digiKam web
    // service plugins use dk-<name> theme icons).
    return QIcon::fromTheme(QLatin1String("dk-immich"));
}

QString ImmichPlugin::description() const
{
    return i18n("A tool to sync albums with an Immich web-service");
}

QString ImmichPlugin::details() const
{
    return i18n("<p>This tool permits to pull albums from a self-hosted Immich server "
                "into digiKam and to push finished items back.</p>"
                "<p>Metadata (tags, rating, caption, GPS) round-trips through XMP "
                "sidecar files, which both digiKam and Immich read and write.</p>"
                "<p>See the Immich web site for details: <a href='https://immich.app/'>https://immich.app/</a></p>");
}

QList<DPluginAuthor> ImmichPlugin::authors() const
{
    return QList<DPluginAuthor>()
            << DPluginAuthor(QString::fromUtf8("Christian McHugh"),
                             QString::fromUtf8(""),
                             QString::fromUtf8("(C) 2026"));
}

void ImmichPlugin::setup(QObject* const parent)
{
    DPluginAction* const ac = new DPluginAction(parent);
    ac->setIcon(icon());
    ac->setText(i18nc("@action", "Sync with &Immich..."));
    ac->setObjectName(QLatin1String("sync_immich"));
    ac->setActionCategory(DPluginAction::GenericExport);

    connect(ac, SIGNAL(triggered(bool)),
            this, SLOT(slotImmich()));

    addAction(ac);
}

void ImmichPlugin::slotImmich()
{
    if (!reactivateToolDialog(m_toolDlg))
    {
        delete m_toolDlg;
        m_toolDlg = new ImmichWindow(infoIface(sender()), nullptr);
        m_toolDlg->setPlugin(this);
        m_toolDlg->show();
    }
}

} // namespace DigikamGenericImmichPlugin
