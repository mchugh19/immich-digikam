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

#ifndef DIGIKAM_IMMICH_WINDOW_H
#define DIGIKAM_IMMICH_WINDOW_H

// Qt includes

#include <QDialog>

// Local includes

#include "immichitem.h"

namespace Digikam
{
class DInfoInterface;
class DPlugin;
}

using namespace Digikam;

namespace DigikamGenericImmichPlugin
{

class ImmichSession;
class ImmichTalker;

/**
 * Tool dialog: connection settings (server URL + API key) and the
 * server's album list. Album selection is where pull and push will
 * operate; the item transfer itself is the next step after this
 * scaffold compiles (it needs the DInfoInterface selection and
 * digiKam's metadata engine for sidecar generation).
 */
class ImmichWindow : public QDialog
{
    Q_OBJECT

public:

    explicit ImmichWindow(DInfoInterface* const iface, QWidget* const parent = nullptr);
    ~ImmichWindow() override;

    void setPlugin(DPlugin* const plugin);

private Q_SLOTS:

    void slotSaveSettings();
    void slotRefreshAlbums();
    void slotAlbums(const QList<ImmichAlbum>& albums);
    void slotError(const QString& msg);
    void slotBusy(bool val);

private:

    class Private;
    Private* const d;
};

} // namespace DigikamGenericImmichPlugin

#endif // DIGIKAM_IMMICH_WINDOW_H
