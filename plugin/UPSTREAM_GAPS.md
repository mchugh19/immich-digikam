# Upstreaming gap list

What separates this plugin, as shipped in this repository, from a
mergeable contribution to digiKam itself (graphics/digikam on KDE
Invent, target location `core/dplugins/generic/webservices/immich/`,
registered by one `add_subdirectory(immich)` line in
`core/dplugins/generic/webservices/CMakeLists.txt`).

Reference shape: the Piwigo web-service plugin, the closest in-tree
precedent (another self-hosted photo server, same
plugin/session/talker/window decomposition). Verified against the
digiKam master tree on 2026-10-18.

## Process (from digiKam's Contribute page and CONTRIBUTING.md)

- Patches are based on the latest git/master, never a stable tag.
- Preferred route is a merge request from a fork on KDE Invent;
  a patch on Bugzilla is the alternative. No patches by mail.
- Commits stay small and atomic; formatting never mixes with code
  changes in one commit.
- A whole new plugin warrants a maintainer conversation first
  (digikam-devel / Bugzilla wish): the team inherits maintenance,
  and Immich's API moves quickly.

## Gaps

1. **SPDX license headers.** In-tree files carry a banner with
   `SPDX-FileCopyrightText:` and `SPDX-License-Identifier:
   GPL-2.0-or-later` lines (CMake files: BSD-3-Clause), and the
   tree is REUSE-clean. This repo's banners carry the full GPL
   paragraph instead. Status: **addressed in this repo** (SPDX
   lines added, paragraph dropped); the banner's project line
   changes to "digiKam project" when the code lands in-tree.

2. **Internationalization.** User-visible strings upstream go
   through `i18n()`/`i18nc()` with `@action`/`@title`/`@info`
   contexts so KDE's translation infrastructure picks them up.
   This plugin's window originally used plain `QStringLiteral`
   throughout. Status: **addressed in this repo** for the window
   and the user-visible talker errors; JSON keys, config keys and
   other non-visible literals stay `QStringLiteral`, as upstream.
   A `Messages.sh` in this directory drives extraction.

3. **In-tree build files.** In-tree plugins build with
   `APPLY_COMMON_POLICIES()`, `include(MacroDPlugins)` and
   `DIGIKAM_ADD_GENERIC_PLUGIN(NAME ... SOURCES ...)`, and their
   `.cpp` files end with `#include "moc_<file>.cpp"`. This repo
   builds standalone with AUTOMOC against the installed
   digikam-devel packages. Status: **addressed in this repo** by
   the drop-in variant under `upstream/` (plus the icons build
   file); the moc include lines are the one source change the
   drop-in still needs, noted in `upstream/README.md`.

4. **Theme icons in KDE sizes.** In-tree web-service plugins ship
   `icons/16-apps-dk-<name>.png`, 22, 32, 48 and
   `sc-apps-dk-<name>.svgz`, installed with `ecm_install_icons`,
   and `icon()` returns `QIcon::fromTheme("dk-<name>")`. This repo
   embeds one SVG in the binary so a hand-copied `.so` still shows
   its icon. Status: **addressed in this repo**: the size set is
   generated under `icons/` and installed by the upstream build
   file; the standalone build keeps the embedded resource, which
   is harmless in-tree too.

5. **Handbook hooks.** In-tree plugins override
   `handbookSection()` ("export_tools") and `handbookChapter()`
   ("immich_export"), and the manual chapter itself lives in the
   separate digikam documentation repository. Status: hooks
   **addressed in this repo** if the 9.1 headers expose the
   virtuals (checked before adding); the manual chapter is a
   follow-up contribution of its own.

6. **Web-services framework dialog.** In-tree web-service windows
   derive from `WSToolDialog` and reuse the shared framework in
   `core/libs/dplugins/webservices` (settings widgets, new-album
   dialog, login dialogs). This plugin's window is a plain
   `DPluginDialog` with its own session class, because Immich
   authenticates with a pasted API key and has no OAuth or
   login-dialog flow for the framework to run. Status: **open,
   deliberately**. This is the one architectural conversation to
   have with the maintainers (in the feeler post / MR
   description) rather than something to preemptively rewrite;
   the dialog otherwise follows the DPlugin conventions
   (`setPlugin`, `reactivateToolDialog`, one action per category).

7. **Coding style.** digiKam has no formatter configuration in
   the tree; style is a documented convention checked in review
   (and by the KDE CI krazy/clazy configurations). This code was
   written imitating the in-tree plugins (banner, sectioned
   includes, d-pointer, aligned declarations) and should expect
   ordinary style review comments. Status: **by-eye conformance**;
   no mechanical action available or needed before the MR.

8. **Tests.** In-tree web-service plugins ship no unit tests
   (Piwigo has none). This repo's suites (pure-logic tests and
   the talker contract tests against a stub server) stay here;
   they can be offered in the MR discussion. Status: **no
   action**.

## What "done" means for the MR

Items 1 to 5 closed (done in this repo, in the drop-in form),
item 6 raised explicitly in the MR description, a maintainer
feeler posted beforehand, and the whole plugin rebuilt and
retested against a digiKam built from master, since that, not
9.1, is the merge target.
