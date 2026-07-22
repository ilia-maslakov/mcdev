#!/bin/bash
# Generate release notes from merged PRs, grouped by type and area.
#
# usage: release_notes.sh <version> [<git-range>]
#   release_notes.sh v4.8.33-dev.2 v4.8.33-dev.1..HEAD
#
# Without a git range all merged PRs are included (first release).
# PR numbers are taken from merge/squash commit subjects in the range,
# titles and labels are fetched from GitHub.
#
# Top level is the type of change (Features, Fixes, Internal); inside a
# type entries are grouped by area, and plugin entries by plugin name.
#
# Type labels: Feature, "Bug fix"; everything else goes to Internal.
# Area labels: mcedit, mcview, panel, vfs, core, Plugin.
#
# PRs labelled "infra" (CI jobs, dependabot version bumps, and anything
# else that is not a change to the program) are left out entirely: they
# are not part of what a release delivers. How many were dropped is
# reported on stderr, so the omission is never silent.
#
# The output is a starting point, not the finished page: each entry is
# the raw PR title. Rewrite the entries into plain language (what the
# user sees, and why it changed) before committing the wiki page.
#
# Output goes to stdout; redirect into the wiki page, e.g.:
#   maint/release_notes.sh v4.8.33-dev.2 v4.8.33-dev.1..HEAD \
#       > ../mcdev.wiki/Release-v4.8.33-dev.2.md
#
# Requires: gh (authenticated or GH_TOKEN set), jq, gawk.

set -e

REPO="ilia-maslakov/mcdev"
VERSION="$1"
RANGE="$2"

if [ -z "$VERSION" ]; then
    echo "usage: $0 <version> [<git-range>]" >&2
    exit 1
fi

prs=$(gh pr list -R "$REPO" --state merged --limit 500 --json number,title,labels)

if [ -n "$RANGE" ]; then
    nums=$(git log --pretty='%s%n%b' "$RANGE" | grep -oE '#[0-9]+' | tr -d '#' | sort -un)
    if [ -z "$nums" ]; then
        echo "no PR references found in range $RANGE" >&2
        exit 1
    fi
    keep=$(printf '%s\n' $nums | jq -Rs '[split("\n")[] | select(length > 0) | tonumber]')
    prs=$(echo "$prs" | jq --argjson keep "$keep" '[.[] | select(.number as $n | $keep | index($n))]')
fi

echo "$prs" | jq -r '.[] | [.number, ([.labels[].name] | join("|")), (.title | gsub("\\s+"; " "))] | @tsv' |
gawk -F'\t' -v version="$VERSION" -v date="$(date +%Y-%m-%d)" -v repo="$REPO" '
function plugin_group(title,   lt, i, n, words) {
    lt = tolower(title)
    n = split("git docker k8s mongodb s3 sftp ftp samba arcmc ctags panelize hello", words, " ")
    for (i = 1; i <= n; i++)
        if (index(lt, words[i]) > 0)
            return words[i]
    return "plugin API / infra"
}
function entry(title, num) {
    return sprintf("- %s ([#%s](https://github.com/%s/pull/%s))\n", title, num, repo, num)
}
# Exact match against one label of the "a|b|c" list, so a label never
# matches by being a substring of another one.
function has_label(labels, name,   i, n, parts) {
    n = split(labels, parts, "|")
    for (i = 1; i <= n; i++)
        if (parts[i] == name)
            return 1
    return 0
}
{
    num = $1; labels = $2; title = $3

    if (has_label(labels, "infra")) {
        dropped++
        next
    }

    if (labels ~ /Bug fix/)      type = "Fixes"
    else if (labels ~ /Feature/) type = "Features"
    else                         type = "Internal"

    if (type == "Internal")           area = "Internal"
    else if (labels ~ /Plugin/)       area = "Plugins"
    else if (labels ~ /mcedit/)       area = "mcedit"
    else if (labels ~ /mcview/)       area = "mcview"
    else if (labels ~ /panel/)        area = "Panel"
    else if (labels ~ /vfs/)          area = "VFS"
    else                              area = "Core"

    if (area == "Plugins") {
        grp = plugin_group(title)
        pgroups[grp] = 1
        plugins[type, grp] = plugins[type, grp] entry(title, num)
    } else if (area == "Internal") {
        internal = internal entry(title, num)
    } else {
        items[type, area] = items[type, area] entry(title, num)
    }
}
END {
    printf "# mcdev %s (%s)\n", version, date

    na = split("mcedit mcview Panel VFS Core", areas, " ")
    ng = asorti(pgroups, gkeys)
    nt = split("Features Fixes", types, " ")

    for (t = 1; t <= nt; t++) {
        type = types[t]

        body = ""
        for (i = 1; i <= na; i++) {
            a = areas[i]
            if (items[type, a] != "")
                body = body sprintf("\n### %s\n\n%s", a, items[type, a])
        }

        pbody = ""
        for (i = 1; i <= ng; i++) {
            g = gkeys[i]
            if (plugins[type, g] != "")
                pbody = pbody sprintf("\n#### %s\n\n%s", g, plugins[type, g])
        }
        if (pbody != "")
            body = body "\n### Plugins\n" pbody

        if (body != "")
            printf "\n## %s\n%s", type, body
    }

    if (internal != "")
        printf "\n## Internal\n\n%s", internal

    if (dropped > 0)
        printf "note: %d PR(s) labelled \"infra\" left out of the notes\n", dropped > "/dev/stderr"
}'
