SUBDIRS = systemd
SUBDIRS += hello

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

SUBDIRS += docker
SUBDIRS += git
SUBDIRS += shell-link

DIST_SUBDIRS = systemd hello sftp samba ftp arcmc s3 docker git shell-link
