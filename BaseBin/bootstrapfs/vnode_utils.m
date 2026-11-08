//
//  vnode_utils.m
//  bootstrapfs
//
//  Every read and write bootstrapfs performs goes through the vnode, resolved
//  from libjailbreak at runtime
//

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dirutils.h"
#include "vnode_utils.h"

// vnode private offsets, these are the same on every iOS version
#define VNODE_V_DATA       0xE0
#define VNODE_DATA_UID     0x80
#define VNODE_DATA_GID     0x84
#define VNODE_DATA_MODE    0x88

static uint64_t (*get_vnode_for_path)(const char *path);
static bool (*vnode_chown)(uint64_t vnode, uid_t uid, gid_t gid);
static bool (*vnode_chmod)(uint64_t vnode, mode_t mode);
static uint64_t (*vnode_redirect_folder)(const char *to, const char *from);
static bool (*vnode_unredirect_folder)(const char *folder, uint64_t orig);
static int (*jbclient_initialize_primitives_internal)(bool physrwPTE);
static uint64_t (*kread64)(uint64_t va);
static uint64_t (*kread_ptr)(uint64_t va);
static uint32_t (*kread32)(uint64_t va);

static bool vnodeCallsReady = false;

int vnode_calls_init(void) {
    if (vnodeCallsReady) return 0;

    char *libjailbreakPath = getItemInJBROOT("/basebin/libjailbreak.dylib");
    if (!libjailbreakPath || libjailbreakPath[0] == '\0') {
        fprintf(stderr, "[-] FAIL: unable to locate libjailbreak in the jailbreak root\n");
        return -1;
    }

    // The handle is kept open for the lifetime of the process, the resolved
    // function pointers point into it
    void *LJBHandler = dlopen(libjailbreakPath, RTLD_NOW);
    if (!LJBHandler) {
        fprintf(stderr, "[-] FAIL: unable to dlopen libjailbreak: %s\n", dlerror());
        return -1;
    }

    get_vnode_for_path                      = dlsym(LJBHandler, "get_vnode_for_path");
    vnode_chown                             = dlsym(LJBHandler, "vnode_chown");
    vnode_chmod                             = dlsym(LJBHandler, "vnode_chmod");
    vnode_redirect_folder                   = dlsym(LJBHandler, "vnode_redirect_folder");
    vnode_unredirect_folder                 = dlsym(LJBHandler, "vnode_unredirect_folder");
    jbclient_initialize_primitives_internal = dlsym(LJBHandler, "jbclient_initialize_primitives_internal");
    kread64                                 = dlsym(LJBHandler, "kread64");
    kread_ptr                               = dlsym(LJBHandler, "kread_ptr");
    kread32                                 = dlsym(LJBHandler, "kread32");

    if (!get_vnode_for_path || !vnode_chown || !vnode_chmod
     || !vnode_redirect_folder || !vnode_unredirect_folder
     || !jbclient_initialize_primitives_internal
     || !kread64 || !kread_ptr || !kread32) {
        fprintf(stderr, "[-] FAIL: libjailbreak is missing vnode symbols\n");
        return -1;
    }

    // vnode access goes straight to the kernel, so this process has to own a
    // primitive instead of relying on a privileged mount. SPTM based physrw only
    // exists from iOS 16 on.
    bool physrwPTE = false;
    if (@available(iOS 16.0, *)) {
        physrwPTE = true;
    }

    int r = jbclient_initialize_primitives_internal(physrwPTE);
    if (r != 0) {
        fprintf(stderr, "[-] FAIL: unable to initialize kernel primitives: %i\n", r);
        return -1;
    }

    vnodeCallsReady = true;
    return 0;
}

uint64_t vnode_read_for_path(const char *path) {
    if (!path) return (uint64_t)-1;
    if (vnode_calls_init() != 0) return (uint64_t)-1;

    return get_vnode_for_path(path);
}

uint64_t vnode_read_data_for_path(const char *path) {
    if (!path) return 0;
    if (vnode_calls_init() != 0) return 0;

    uint64_t vnode = get_vnode_for_path(path);
    if (vnode == 0 || vnode == (uint64_t)-1) return 0;

    // vnode->v_data, signed on arm64e
    return kread_ptr(vnode + VNODE_V_DATA);
}

bool vnode_read_attrs(const char *path, uid_t *uidOut, gid_t *gidOut, mode_t *modeOut) {
    if (!path) return false;

    uint64_t vData = vnode_read_data_for_path(path);
    if (vData == 0) return false;

    if (uidOut)  *uidOut  = kread32(vData + VNODE_DATA_UID);
    if (gidOut)  *gidOut  = kread32(vData + VNODE_DATA_GID);
    if (modeOut) *modeOut = kread32(vData + VNODE_DATA_MODE);

    return true;
}

bool vnode_path_exists(const char *path) {
    // Existence is answered from the vnode table, a path based check reports the
    // wrong answer once the folder has been redirected
    return vnode_read_data_for_path(path) != 0;
}

bool vnode_write_attrs(const char *path, uid_t uid, gid_t gid, mode_t mode) {
    if (!path) return false;
    if (vnode_calls_init() != 0) return false;

    uint64_t vnode = get_vnode_for_path(path);
    if (vnode == 0 || vnode == (uint64_t)-1) {
        fprintf(stderr, "[-] FAIL: unable to get vnode for %s\n", path);
        return false;
    }

    return vnode_chown(vnode, uid, gid) && vnode_chmod(vnode, mode);
}

int vnode_redirect_folder_at_path(const char *path, const char *fromPath, uid_t uid, gid_t gid, mode_t mode, uint64_t *origDataOut) {
    if (!path || !fromPath) return -1;
    if (vnode_calls_init() != 0) return -1;

    uint64_t origData = vnode_redirect_folder(path, fromPath);
    if (origData == (uint64_t)-1) {
        fprintf(stderr, "[-] FAIL: unable to redirect %s to %s\n", path, fromPath);
        return -1;
    }

    // A redirected folder only differs inside the kernel, so its ownership has
    // to be written to the vnode as well
    if (!vnode_write_attrs(path, uid, gid, mode)) {
        // Do not leave a half applied redirect behind
        vnode_unredirect_folder(path, origData);
        return -1;
    }

    if (origDataOut) *origDataOut = origData;
    return 0;
}

int vnode_unredirect_folder_at_path(const char *path, uint64_t origData) {
    if (!path || origData == 0) return -1;
    if (vnode_calls_init() != 0) return -1;

    return vnode_unredirect_folder(path, origData) ? 0 : -1;
}
