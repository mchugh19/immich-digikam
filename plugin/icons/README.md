# Icons

`dk-immich.svg` is the plugin's theme icon: a photo frame with a
sync badge. The plugin looks it up as `dk-immich` (see
`ImmichPlugin::icon()`); the CMake install rule places it in
`share/icons/hicolor/scalable/apps`, where digiKam's other `dk-*`
icons live. When the icon is not installed, the Export action
simply shows no icon.
