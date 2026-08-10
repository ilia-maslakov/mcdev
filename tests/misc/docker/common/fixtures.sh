#!/bin/sh
# Build the sandbox scenarios under $1.
#
# One directory per scenario, each with a cases.tsv saying what the files in it
# are for: name, key, expected outcome, reason.  A person reads it as a
# checklist; something automated can walk the directories and read the same
# columns.
set -e

dir="${1:-/home/mc/archives}"
rm -rf "$dir"
mkdir -p "$dir"
cd "$dir"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# what goes inside every archive
mkdir -p "$work/payload/каталог с пробелами"
head -c 3000000 /dev/urandom > "$work/payload/a.bin"
head -c 1500000 /dev/urandom > "$work/payload/b.bin"
printf 'hello from the sandbox\n' > "$work/payload/readme.txt"
printf 'кириллица внутри архива\n' > "$work/payload/привет.txt"
printf 'и ещё одна строка\n' > "$work/payload/каталог с пробелами/файл.txt"

mkdir -p "$work/payload_big"
i=1
while [ "$i" -le 10 ]; do
    head -c 1000000 /dev/urandom > "$work/payload_big/part$i.bin"
    i=$((i + 1))
done

pack ()
{
    (cd "$work" && bsdtar "$@")
}

# ---------------------------------------------------------------- formats ---

mkdir -p 01-formats
pack -a -cf "$dir/01-formats/small.tar.gz" payload
pack -a -cf "$dir/01-formats/small.zip" payload
pack --format 7zip -cf "$dir/01-formats/small.7z" payload
pack --format 7zip -cf "$dir/01-formats/big.7z" payload_big

cat > 01-formats/cases.tsv <<'EOF'
file	key	expect	why
small.tar.gz	Enter	archive panel	reads in one pass, no seeking needed
small.zip	Enter	archive panel	same
small.7z	Enter	archive panel	directory at the end of the file: needs seek
big.7z	Enter	archive panel	past libarchive's read-ahead buffer
small.tar.gz	F3	listing	the view operation, not archive.sh
EOF

# ---------------------------------------------------------------- content ---

mkdir -p 02-content
cp 01-formats/small.tar.gz 02-content/noext
cp 01-formats/big.7z 02-content/sevenzip-without-suffix
printf 'plain text, whatever the name says\n' > 02-content/notanarchive.tar.gz

cat > 02-content/cases.tsv <<'EOF'
file	key	expect	why
noext	Enter	archive panel	a stream is taken by content; a local file by name, so nothing here
sevenzip-without-suffix	Enter	archive panel	same, over sftp or shell only
notanarchive.tar.gz	Enter	nothing, no error	the name lies and the operation turns it down
EOF

# ----------------------------------------------------------------- nested ---

mkdir -p 03-nested
(cd 01-formats && bsdtar -cf "$dir/03-nested/outer.tar" small.zip small.7z)
(cd 01-formats && bsdtar -a -cf "$dir/03-nested/zip-in-zip.zip" small.zip small.tar.gz)

cat > 03-nested/cases.tsv <<'EOF'
file	key	expect	why
outer.tar	Enter	archive panel	then Enter on small.zip inside it
outer.tar	..	the panel it came from	twice: inner archive, outer archive, then sftp or ftp
zip-in-zip.zip/uzip://	cd	extfs panel	utar:// is gone, uzip:// is the filesystem left to try
small.zip inside uzip://	Enter	archive panel	a file inside an mc filesystem is left to mc.ext.ini
EOF

# --------------------------------------------------------------- non-ascii ---

mkdir -p 04-non-ascii
cp 01-formats/small.tar.gz '04-non-ascii/архив.tar.gz'
cp 01-formats/small.7z '04-non-ascii/архив с пробелами.7z'
printf 'просто текст\n' > '04-non-ascii/заметка.txt'
mkdir -p '04-non-ascii/каталог'
printf 'вложенный файл\n' > '04-non-ascii/каталог/файл.txt'

cat > 04-non-ascii/cases.tsv <<'EOF'
file	key	expect	why
архив.tar.gz	Enter	archive panel	the name survives the panel, the quoting and the shell
архив с пробелами.7z	Enter	archive panel	spaces as well as Cyrillic
архив.tar.gz	F5	copy to the other panel	the name reaches a file operation intact
заметка.txt	Ctrl-O then ls	the name as written	the subshell is zsh here
EOF

find "$dir" -mindepth 1 -maxdepth 2 | sort
