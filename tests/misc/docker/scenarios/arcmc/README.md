# arcmc

The everyday scenario: current Debian, UTF-8, zsh, and every tool an archive
might need installed.  A remote host serves the same scenarios over sftp, ssh,
ftp and samba, and mc is built from the working tree against it.

    tests/misc/docker/sandbox.sh arcmc up
    tests/misc/docker/sandbox.sh arcmc mc

What it is meant to catch: whether a panel plugin can read an archive it does
not hold, over each of the four protocols, and what happens when the archive is
inside another one.

It is deliberately a comfortable environment.  A scenario that takes tools
away, or moves to older libraries, or makes the far end misbehave, belongs in
its own directory next to this one -- this one stays as it is.
