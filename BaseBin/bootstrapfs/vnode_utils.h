#ifndef VNODE_UTILS_H
#define VNODE_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

// Resolves the vnode functions out of libjailbreak and sets up the kernel
// primitives they need. Safe to call more than once, 0 means the vnode layer
// is usable.
int vnode_calls_init(void);

// vnode read
uint64_t vnode_read_for_path(const char *path);
uint64_t vnode_read_data_for_path(const char *path);
bool vnode_read_attrs(const char *path, uid_t *uidOut, gid_t *gidOut, mode_t *modeOut);
bool vnode_path_exists(const char *path);

// vnode write
bool vnode_write_attrs(const char *path, uid_t uid, gid_t gid, mode_t mode);
int vnode_redirect_folder_at_path(const char *path, const char *fromPath, uid_t uid, gid_t gid, mode_t mode, uint64_t *origDataOut);
int vnode_unredirect_folder_at_path(const char *path, uint64_t origData);

#endif /* VNODE_UTILS_H */
