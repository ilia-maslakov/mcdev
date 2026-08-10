FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        autoconf automake libtool pkg-config gettext autopoint \
        libglib2.0-dev libncurses-dev \
        libssh2-1-dev libcurl4-openssl-dev libarchive-dev libmagic-dev libsqlite3-dev libsmbclient-dev \
        check rsync file openssh-client curl sshpass smbclient ca-certificates \
        zsh libarchive-tools procps unzip zip \
        locales \
    && sed -i 's/^# *\(en_US.UTF-8\|ru_RU.UTF-8\)/\1/' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/*

# without a UTF-8 locale mc draws question marks instead of anything non-ASCII
ENV LANG=ru_RU.UTF-8
ENV LC_ALL=ru_RU.UTF-8

COPY common/build-mc.sh common/check-remote.sh common/fixtures.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/build-mc.sh /usr/local/bin/check-remote.sh /usr/local/bin/fixtures.sh

# mc drives its subshell from $SHELL; a fresh zsh would stop at its
# new-user questionnaire, so give it an rc file of its own.
COPY common/zshrc /root/.zshrc
ENV SHELL=/usr/bin/zsh

ENV TERM=xterm-256color
WORKDIR /work

CMD ["/bin/bash"]
