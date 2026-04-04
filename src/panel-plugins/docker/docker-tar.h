#ifndef MC__PANEL_PLUGINS_DOCKER_TAR_H
#define MC__PANEL_PLUGINS_DOCKER_TAR_H

#include "lib/global.h"

#define DOCKER_TAR_BLOCK_SIZE 512

struct docker_tar_header
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

gboolean docker_tar_read_full (int fd, void *buf, size_t count);
gboolean docker_tar_skip (int fd, guint64 count);
gboolean docker_tar_is_zero_block (const void *block);
guint64 docker_tar_parse_octal (const char *field, size_t len, gboolean *ok);
char *docker_tar_header_get_path (const struct docker_tar_header *hdr,
                                  const char *longname_override);
char *docker_tar_header_get_linkname (const struct docker_tar_header *hdr,
                                      const char *longlink_override);
char *docker_tar_read_longname (int fd, guint64 size);

#endif /* MC__PANEL_PLUGINS_DOCKER_TAR_H */
