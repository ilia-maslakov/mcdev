SUBDIRS = systemd
SUBDIRS += hello
SUBDIRS += panelize

if ENABLE_VFS_SFTP
SUBDIRS += sftp
endif

if ENABLE_PANEL_PLUGIN_SAMBA
SUBDIRS += samba
endif

if ENABLE_PANEL_PLUGIN_FTP
SUBDIRS += ftp
endif

if ENABLE_PANEL_PLUGIN_ARCMC
SUBDIRS += arcmc
endif

if ENABLE_PANEL_PLUGIN_S3
SUBDIRS += s3
endif

if ENABLE_PANEL_PLUGIN_MONGO
SUBDIRS += mongo
endif

if ENABLE_PANEL_PLUGIN_SQLITE
SUBDIRS += sqlite
endif

SUBDIRS += docker
SUBDIRS += git
SUBDIRS += k8s
if ENABLE_PANEL_PLUGIN_SHELL_LINK
SUBDIRS += shell-link
endif

DIST_SUBDIRS = systemd hello panelize sftp samba ftp arcmc s3 mongo sqlite docker git k8s shell-link
