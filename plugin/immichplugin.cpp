/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * Date        : 2026-10-18
 *
 * SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ============================================================ */


// Self includes

#include "immichplugin.h"

// KDE includes

#include <klocalizedstring.h>

// Local includes

#include "immichsession.h"
#include "immichwindow.h"

namespace DigikamGenericImmichPlugin
{

class ImmichPlugin::Private
{
public:

    ~Private()
    {
        // Only the session is deleted here: it is a plain
        // settings object. The windows are not owned and must
        // not be deleted from the plugin destructor, which runs
        // while the plugin library is being unloaded at process
        // exit, when deleting widgets crashes in Qt's GUI
        // teardown. The windows carry WA_DeleteOnClose and
        // destroy themselves; the QPointers below simply go
        // null when one closes.
        delete session;
    }

    QPointer<ImmichWindow> toolDlgExport;
    QPointer<ImmichWindow> toolDlgImport;
    ImmichSession*         session = nullptr;
};

ImmichPlugin::ImmichPlugin(QObject* const parent)
    : DPluginGeneric(parent),
      d(new Private)
{
}

ImmichPlugin::~ImmichPlugin()
{
    delete d;
}

QString ImmichPlugin::name() const
{
    return i18nc("@title", "Immich");
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
    return i18nc("@info", "A tool to export and import items with an Immich web-service");
}

QString ImmichPlugin::details() const
{
    return i18nc("@info", "<p>This tool permits to pull albums from a self-hosted Immich server "
                "into digiKam and to push finished items back.</p>"
                "<p>Metadata (tags, rating, caption, GPS) round-trips through XMP "
                "sidecar files, which both digiKam and Immich read and write.</p>"
                "<p>See the Immich web site for details: <a href='https://immich.app/'>https://immich.app/</a></p>");
}

QString ImmichPlugin::handbookSection() const
{
    return QLatin1String("export_tools");
}

QString ImmichPlugin::handbookChapter() const
{
    return QLatin1String("immich_export");
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

ImmichSession* ImmichPlugin::session()
{
    if (!d->session)
    {
        d->session = new ImmichSession;
        d->session->load();
    }

    return d->session;
}

void ImmichPlugin::slotExport()
{
    if (!reactivateToolDialog(d->toolDlgExport))
    {
        delete d->toolDlgExport;
        d->toolDlgExport = new ImmichWindow(infoIface(sender()), session(),
                                            ImmichWindow::Mode::Export, nullptr);
        d->toolDlgExport->setPlugin(this);
        d->toolDlgExport->show();
    }
}

void ImmichPlugin::slotImport()
{
    if (!reactivateToolDialog(d->toolDlgImport))
    {
        delete d->toolDlgImport;
        d->toolDlgImport = new ImmichWindow(infoIface(sender()), session(),
                                            ImmichWindow::Mode::Import, nullptr);
        d->toolDlgImport->setPlugin(this);
        d->toolDlgImport->show();
    }
}

} // namespace DigikamGenericImmichPlugin
