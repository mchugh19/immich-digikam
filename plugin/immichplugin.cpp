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
    delete m_toolDlgExport;
    delete m_toolDlgImport;
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
    // The icon is embedded in the plugin binary (qt_add_resources
    // in CMakeLists.txt), so it is available however the .so was
    // installed, including a manual copy that never runs the
    // theme install rule.
    return QIcon(QStringLiteral(":/dk-immich.svg"));
}

QString ImmichPlugin::description() const
{
    return i18n("A tool to export and import items with an Immich web-service");
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
    // digiKam separates import and export tools: one plugin
    // registers one action of each category, and each opens the
    // shared dialog in its own mode. This is the arrangement the
    // Google web-service plugin uses.
    DPluginAction* const exportAction = new DPluginAction(parent);
    exportAction->setIcon(icon());
    exportAction->setText(i18nc("@action", "Export to &Immich..."));
    exportAction->setObjectName(QLatin1String("export_immich"));
    exportAction->setActionCategory(DPluginAction::GenericExport);

    connect(exportAction, SIGNAL(triggered(bool)),
            this, SLOT(slotExport()));

    addAction(exportAction);

    DPluginAction* const importAction = new DPluginAction(parent);
    importAction->setIcon(icon());
    importAction->setText(i18nc("@action", "Import from &Immich..."));
    importAction->setObjectName(QLatin1String("import_immich"));
    importAction->setActionCategory(DPluginAction::GenericImport);

    connect(importAction, SIGNAL(triggered(bool)),
            this, SLOT(slotImport()));

    addAction(importAction);
}

void ImmichPlugin::slotExport()
{
    if (!reactivateToolDialog(m_toolDlgExport))
    {
        delete m_toolDlgExport;
        m_toolDlgExport = new ImmichWindow(infoIface(sender()), ImmichWindow::Mode::Export, nullptr);
        m_toolDlgExport->setPlugin(this);
        m_toolDlgExport->show();
    }
}

void ImmichPlugin::slotImport()
{
    if (!reactivateToolDialog(m_toolDlgImport))
    {
        delete m_toolDlgImport;
        m_toolDlgImport = new ImmichWindow(infoIface(sender()), ImmichWindow::Mode::Import, nullptr);
        m_toolDlgImport->setPlugin(this);
        m_toolDlgImport->show();
    }
}

} // namespace DigikamGenericImmichPlugin
