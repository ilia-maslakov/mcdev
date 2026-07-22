# Midnight Commander (fork)

A modified fork of [GNU Midnight Commander](https://midnight-commander.org),
based on version 4.8.33.

Midnight Commander (MC) is a text-mode, full-screen file manager: two panels,
a built-in editor and viewer, and a virtual filesystem for browsing archives
and remote hosts. It runs on the OS console, in xterm, and over ssh.

This fork is not an official GNU package. Report issues here, not upstream.

Own version numbering starts at `v6.0.1`, independent of upstream.

![Git panel with inline diff](https://raw.githubusercontent.com/wiki/ilia-maslakov/mcdev/assets/git-panel.gif)

## What this fork adds

Full notes: **[Releases wiki](https://github.com/ilia-maslakov/mcdev/wiki/Releases)**.

- **Panel plugins.** Panel contents can come from a dynamically loaded plugin.
  Shipped: git, docker, Kubernetes, MongoDB, S3, FTP/FTPS, SFTP, Samba, systemd,
  shell connections, External Panelize, and arcmc. The old built-in `ftpfs` and
  `sftpfs` VFS modules are replaced by the FTP and SFTP plugins.
- **arcmc** — an archive manager on libarchive: browse, create, pack and extract
  (zip, 7z, tar.\*, cpio) with progress and cancel.

  ![arcmc](https://raw.githubusercontent.com/wiki/ilia-maslakov/mcdev/assets/arcmc.gif)
- **Editor** — code folding, an undo history browser, a macro explorer, and an
  editor plugin framework.
- **Viewer** — a structured tree mode for JSON, YAML, XML and HTML, a grep-style
  live filter, ANSI colour and terminal replay, and streaming of never-ending
  command output.

  ![Structured tree viewer](https://raw.githubusercontent.com/wiki/ilia-maslakov/mcdev/assets/viewer-tree.gif)

  ![Grep-style live filter](https://raw.githubusercontent.com/wiki/ilia-maslakov/mcdev/assets/viewer-filter.gif)
- **Embedded terminal** — run a shell inside the file manager, panels stay in
  sync with its directory.
- **Panels** — user-editable view modes, dialogs for managing key bindings and
  learning terminal keys, and the classic hide-a-panel / run-a-command flow.

  ![Hide a panel, run a command](https://raw.githubusercontent.com/wiki/ilia-maslakov/mcdev/assets/panel-hide.gif)

## Building

See [`INSTALL`](INSTALL) for dependencies and instructions.

```sh
./autogen.sh          # from a git checkout
./configure
make
sudo make install
```

`mc --version` identifies this fork.

## Documentation

- [Wiki](https://github.com/ilia-maslakov/mcdev/wiki)
- Built-in help: press `F1` inside mc
- Manual pages: `mc(1)`, `mcedit(1)`, `mcview(1)`

## Reporting problems

Open an issue: <https://github.com/ilia-maslakov/mcdev/issues>

Include `mc --version`, your OS and distribution, and the compiler and
configure flags if you know them. For a crash, attach a `gdb` backtrace
(`gdb mc core`, then `where`).

## License

GNU General Public License, version 3 or any later version. See
[`COPYING`](COPYING).
