#!/bin/sh
# Drive a sandbox scenario.  Run it from anywhere.
#
# A scenario is a directory under scenarios/ with a docker-compose.yml in it.
# They are found by looking, not by being listed here, so adding one is adding
# a directory and nothing else.
set -e

cd "$(dirname "$0")"
root=$(pwd)

COMPOSE="docker compose"
$COMPOSE version >/dev/null 2>&1 || COMPOSE="docker-compose"

scenarios ()
{
    for d in "$root"/scenarios/*/; do
        [ -f "$d/docker-compose.yml" ] && basename "$d"
    done
}

usage ()
{
    cat <<EOF
usage: sandbox.sh [scenario] <command>

  up        build the images, start the remote host, build mc      (first run)
  mc        run mc against that scenario                           (what you want)
  build     rebuild mc from the working tree, keeping the objects
  check     ask every protocol for a listing, without a terminal
  shell     a shell next to mc, with ssh, curl and smbclient in it
  remote    a shell on the remote host
  logs      what the remote host has to say
  down      stop the containers
  clean     stop them and throw the build away

scenarios: $(scenarios | tr '\n' ' ')
default:   \$MC_SANDBOX (currently ${MC_SANDBOX:-arcmc})

Each scenario is its own compose project, so two of them do not share a
network, a container or a build.  See scenarios/<name>/README.md for what one
is meant to catch.
EOF
}

# The scenario may be named first; otherwise the default one is used.
scenario="${MC_SANDBOX:-arcmc}"
if [ -n "${1:-}" ] && [ -f "$root/scenarios/$1/docker-compose.yml" ]; then
    scenario="$1"
    shift
fi

command="${1:-}"
[ -n "$command" ] && shift || true

if [ "$command" = "list" ]; then
    scenarios
    exit 0
fi

if [ ! -f "$root/scenarios/$scenario/docker-compose.yml" ]; then
    echo "sandbox.sh: no such scenario: $scenario" >&2
    echo "have: $(scenarios | tr '\n' ' ')" >&2
    exit 1
fi

cd "$root/scenarios/$scenario"

case "$command" in
up)
    $COMPOSE build
    $COMPOSE up -d remote
    $COMPOSE run --rm mc /usr/local/bin/build-mc.sh
    echo
    echo "ready: $0 $scenario mc"
    ;;
mc)
    $COMPOSE up -d remote
    $COMPOSE run --rm mc /work/opt/mc/bin/mc "$@"
    ;;
build)
    $COMPOSE run --rm mc /usr/local/bin/build-mc.sh
    ;;
check)
    $COMPOSE up -d remote
    $COMPOSE run --rm mc /usr/local/bin/check-remote.sh
    ;;
shell)
    $COMPOSE up -d remote
    $COMPOSE run --rm mc bash
    ;;
remote)
    $COMPOSE up -d remote
    $COMPOSE exec remote bash
    ;;
logs)
    $COMPOSE logs remote
    ;;
down)
    $COMPOSE down
    ;;
clean)
    $COMPOSE down -v
    ;;
*)
    usage
    exit 1
    ;;
esac
