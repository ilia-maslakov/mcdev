#!/bin/sh
# Ask every protocol the sandbox serves for a listing, so that a failure in mc
# can be told apart from a remote host that was never up.
set -e

host=remote
user=mc
pass=mc
rc=0

report ()
{
    if [ "$2" = 0 ]; then
        printf '  %-12s ok\n' "$1"
    else
        printf '  %-12s FAILED\n' "$1"
        rc=1
    fi
}

out=$(curl -s -u "$user:$pass" "sftp://$host/home/$user/archives/" -l) && [ -n "$out" ]
report sftp $?

out=$(curl -s -u "$user:$pass" "ftp://$host/archives/") && [ -n "$out" ]
report ftp $?

out=$(sshpass -p "$pass" ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        "$user@$host" 'ls ~/archives' 2>/dev/null) && [ -n "$out" ]
report ssh $?

out=$(smbclient "//$host/archives" -U "$user%$pass" -c ls 2>/dev/null) && [ -n "$out" ]
report smb $?

if [ -x /work/opt/mc/bin/mc ]; then
    report mc 0
    echo "  plugins:     $(ls /work/opt/mc/lib/mc/panel-plugins/ | tr '\n' ' ')"
else
    echo "  mc           not built yet -- run: sandbox.sh build"
    rc=1
fi

exit $rc
