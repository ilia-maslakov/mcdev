#!/bin/sh
#
# Strip Serilog "SourceContext" noise from .NET console logs for readable output.
# Input lines look like:
#   [18:22:52 INF] Request finished ... <s:Microsoft.AspNetCore.Hosting.Diagnostics>
# Output drops the trailing "<s:...>" source-context tag:
#   [18:22:52 INF] Request finished ...
# Lines without the tag pass through unchanged.
#
# Usage in the docker "Pipe through" field:  dlog
# Usage from shell:  docker logs -f <container> | dlog
#
# Installation (copy or symlink to a directory on your PATH):
#   cp ~/.local/lib/mc/panel-plugins/docker/dlog.sh ~/.local/bin/dlog
#   chmod +x ~/.local/bin/dlog
# (Or use the system path /usr/lib/mc/panel-plugins/docker/dlog.sh.)

# sed -u keeps it line-buffered so it works with a live "Follow" stream.
exec sed -u -E 's/[[:space:]]*<s:[^>]*>[[:space:]]*$//'
