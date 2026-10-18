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

#ifndef DIGIKAM_IMMICH_PLUGIN_H
#define DIGIKAM_IMMICH_PLUGIN_H

// Local includes

#include "dplugingeneric.h"

#define DPLUGIN_IID "org.kde.digikam.plugin.generic.Immich"

using namespace Digikam;

namespace DigikamGenericImmichPlugin
{

class ImmichWindow;

class ImmichPlugin : public DPluginGeneric
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DPLUGIN_IID)
    Q_INTERFACES(Digikam::DPluginGeneric)

public:

    explicit ImmichPlugin(QObject* const parent = nullptr);
    ~ImmichPlugin() override;

    QString name()                 const override;
    QString iid()                  const override;
    QIcon   icon()                 const override;
    QString details()              const override;
    QString description()          const override;
    QList<DPluginAuthor> authors() const override;

    void setup(QObject* const) override;

private Q_SLOTS:

    void slotExport();
    void slotImport();

private:

    QPointer<ImmichWindow> m_toolDlgExport;
    QPointer<ImmichWindow> m_toolDlgImport;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_PLUGIN_H
