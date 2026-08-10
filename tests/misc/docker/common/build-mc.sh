#!/bin/sh
# Build the tree mounted at /src and install it to /opt/mc.  The sources are
# copied first, so the working tree stays as it was and root-owned build
# output stays in the container volume.
set -e

copy_sources ()
{
    rsync -a --delete \
        --exclude '.git' \
        --exclude 'tests/misc/docker/' \
        /src/ /work/src/
}

echo "== copying sources =="
mkdir -p /work/src /work/build
copy_sources

cd /work/src
[ -x autogen.sh ] || { echo "no autogen.sh in /work/src: is the tree mounted at /src?" >&2; exit 1; }
if [ ! -x configure ] || [ configure.ac -nt configure ]; then
    echo "== autogen =="
    ./autogen.sh
fi

# Everything lives in the volume: the container that builds mc is not the one
# that runs it.
PREFIX=/work/opt/mc

cd /work/build
if [ ! -f config.status ] || [ /work/src/configure -nt config.status ] \
    || ! ./config.status --config 2>/dev/null | grep -q -- "--prefix=$PREFIX"; then
    echo "== configure =="
    /work/src/configure \
        --prefix="$PREFIX" \
        --with-screen=ncurses \
        --enable-panel-plugin-sftp \
        --enable-panel-plugin-arcmc \
        --enable-panel-plugin-ftp \
        --enable-panel-plugin-samba \
        --enable-panel-plugin-shell-link \
        --disable-panel-plugin-mongo
fi

echo "== make =="
make -j"$(nproc)"
make install

# The same archives on a local panel, for what does not need a server.  A
# scenario that exists to check the build may not have an archiver at all.
if [ ! -d /work/local ]; then
    if command -v bsdtar >/dev/null; then
        echo "== local fixtures =="
        /usr/local/bin/fixtures.sh /work/local >/dev/null
    else
        echo "== local fixtures skipped: no bsdtar in this image =="
    fi
fi

echo
echo "mc installed:      $PREFIX/bin/mc"
echo "panel plugins in:  $(ls -d "$PREFIX"/lib/mc/panel-plugins/* 2>/dev/null | tr '\n' ' ')"
