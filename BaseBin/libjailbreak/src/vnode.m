//
//  vnode.m
//  Dopamine3_Rootful
//
//  Created by Alfie on 01/04/2024.
//

#import <Foundation/Foundation.h>
#import "vnode.h"
#import "util.h"
#import "primitives.h"

uint64_t get_vnode_for_path(const char *path) {
    if (!path) return (uint64_t)-1;

    int fd = open(path, O_RDONLY);
    if (fd == -1) return (uint64_t)-1;

    uint64_t vnode = proc_get_vnode_for_fd(proc_self(), fd);

    close(fd);

    if (vnode == 0) return (uint64_t)-1;
    return vnode;
}

uint64_t get_vnode_for_path_by_chdir(const char *path) {
    // This used to chdir into the path and read fd_cdir out of the process struct.
    // That clobbered the caller's working directory and relied on hardcoded offsets,
    // so go through the fd table instead, which uses the dynamically resolved offsets.
    return get_vnode_for_path(path);
}

uint64_t get_child_vnode_from_vnode(uint64_t vnode, const char *file) {
    if (!vnode || !file) return (uint64_t)-1;

    // Walk the children namecache list of the vnode instead of overwriting the cache
    // head every iteration, which used to make this loop forever.
    uint64_t namecache = kread64(vnode + 0x30); // vnode->v_ncchildren_tqh_first
    if (namecache == 0) {
        printf("vp_namecache is 0!\n");
        return (uint64_t)-1;
    }

    for (int i = 0; namecache != 0 && i < 4096; i++) {
        uint64_t child_vnode = kread64(namecache + 0x48); // namecache->nc_vp
        if (child_vnode != 0) {
            char name[16] = { 0 };
            kreadbuf(kread64(namecache + 0x60), &name, sizeof(name) - 1); // namecache->nc_name

            printf("File: %s\n", name);

            if (strcmp(name, file) == 0) {
                printf("Found %s!\n", name);
                return child_vnode;
            }
        }

        namecache = kread64(namecache + 0x10); // namecache->nc_vnc_que.tqe_next
    }

    return (uint64_t)-1;
}

bool vnode_chown(uint64_t vnode, uid_t uid, gid_t gid) {
    uint64_t v_data = UNSIGN_PTR(kread64(vnode + 0xE0)); // vnode->v_data
    if (v_data == 0) return false;

    uint32_t v_uid = kread32(v_data + 0x80); // vnode->v_uid
    uint32_t v_gid = kread32(v_data + 0x84); // vnode->v_gid
    
    // printf("vnode->v_uid %d -> %d\n", v_uid, uid);
    kwrite32(v_data + 0x80, uid);
    // printf("vnode->v_gid %d -> %d\n", v_gid, gid);
    kwrite32(v_data + 0x84, gid);
    
    return kread32(v_data + 0x80) == uid
    && kread32(v_data + 0x84) == gid;
}

bool vnode_chmod(uint64_t vnode, mode_t mode) {
    uint64_t v_data = UNSIGN_PTR(kread64(vnode + 0xE0)); // vnode->v_data
    if (v_data == 0) return false;

    uint32_t v_mode = kread32(v_data + 0x88); // vnode->v_data.v_mode
    
    // printf("vnode->v_mode %o -> %o\n", v_mode, mode);
    kwrite32(v_data + 0x88, mode);
    
    return kread32(v_data + 0x88) == mode;
}

uint64_t vnode_redirect_folder(const char *to, const char *from) {
    if (!to || !from) return (uint64_t)-1;

    uint64_t to_vnode = get_vnode_for_path_by_chdir(to);
    if (to_vnode == (uint64_t)-1) {
        printf("Failed to get vnode for %s\n", to);
        return (uint64_t)-1;
    }
    
    uint64_t orig_to_v_data = kread64(to_vnode + 0xE0); // vnode->v_data
    if (orig_to_v_data == 0) {
        printf("Failed to get v_data for %s\n", to);
        return (uint64_t)-1;
    }
    
    uint64_t from_vnode = get_vnode_for_path_by_chdir(from);
    if (from_vnode == (uint64_t)-1) {
        printf("Failed to get vnode for %s\n", from);
        return (uint64_t)-1;
    }
    
    uint64_t from_v_data = kread64(from_vnode + 0xE0);
    if (from_v_data == 0) {
        printf("Failed to get v_data for %s\n", from);
        return (uint64_t)-1;
    }
    
    if (kwrite64(to_vnode + 0xE0, from_v_data) != 0) {
        printf("Failed to redirect %s to %s\n", to, from);
        return (uint64_t)-1;
    }
    
    // Ownership and permissions are set by the caller through vnode_chown / vnode_chmod,
    // so that every mount can pick the owner it actually needs.
    usleep(2000);
    return orig_to_v_data;
}

bool vnode_unredirect_folder(const char *folder, uint64_t orig) {
    if (!folder || orig == 0) return false;

    uint64_t vnode = get_vnode_for_path_by_chdir(folder);
    if (vnode == (uint64_t)-1) { return false; }
    
    if (kwrite64(vnode + 0xE0, orig) != 0) { return false; } // vnode->v_data

    usleep(2000);
    return true;
}
