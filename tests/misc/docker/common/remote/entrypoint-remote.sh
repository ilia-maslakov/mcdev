#!/bin/sh
set -e

ssh-keygen -A >/dev/null

# vsftpd hands the passive address out itself; from the host use the mapped
# ports, from the mc container use "remote" directly.
vsftpd /etc/vsftpd.conf &
smbd --daemon

exec /usr/sbin/sshd -D -e
