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
    int fd = open(path, O_RDONLY);
    if (fd == -1) return -1;
    
    uint64_t proc = proc_self();
    uint64_t filedesc = UNSIGN_PTR(kread64(proc + 0xF8)); // proc->filedesc
    uint64_t openedFile = kread64(filedesc + (8 * fd)); // filedesc.files[index]
    uint64_t fileglob = UNSIGN_PTR(kread64(openedFile + 0x10)); // filedesc.files[index]->fileglob
    uint64_t vnode = UNSIGN_PTR(kread64(fileglob + 0x38)); // fileglob->vnode
    
    close(fd);
    return vnode;
}

uint64_t get_vnode_for_path_by_chdir(const char *path) {
    if (access(path, F_OK) == -1) {
        return -1;
    }
    if (chdir(path) == -1) { return -1; }
    
    uint64_t fd_cdir_vp = kread64(proc_self() + 0xF8 + 0x20); // proc->p_pfd->fd_cdir
    chdir("/");
    return fd_cdir_vp;
}

uint64_t get_child_vnode_from_vnode(uint64_t vnode, const char *file) {
    uint64_t vp_nameptr = kread64(vnode + 0xB8); // vnode->vp_name
    uint64_t vp_namecache = kread64(vnode + 0x30); // vnode->v_ncchildren_tqh_first
    if (vp_namecache == 0) {
        printf("vp_namecache is 0!\n");
        return -1;
    }
    
    while (1) {
        if (vp_namecache == 0) {
            break;
        }
        vnode = kread64(vp_namecache + 0x48); // vp_namecache->nc_vp
        if (vnode == 0) {
            break;
        }
        vp_nameptr = kread64(vnode + 0xB8);
        
        char vp_name[16];
        kreadbuf(kread64(vp_namecache + 0x60), &vp_name, 16);
        
        printf("File: %s\n", vp_name);
        
        if (strcmp(vp_name, file) == 0) {
            printf("Found %s!\n", vp_name);
            return vnode;
        }
    }
    return vnode;
}

bool vnode_chown(uint64_t vnode, uid_t uid, gid_t gid) {
    uint64_t v_data = kread64(vnode + 0xE0); // vnode->v_data
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
    uint64_t v_data = kread64(vnode + 0xE0); // vnode->v_data
    uint32_t v_mode = kread32(v_data + 0x88); // vnode->v_data.v_mode
    
    // printf("vnode->v_mode %o -> %o\n", v_mode, mode);
    kwrite32(v_data + 0x88, mode);
    
    return kread32(v_data + 0x88) == mode;
}

uint64_t vnode_redirect_folder(const char *to, const char *from) {
    uint64_t to_vnode = get_vnode_for_path_by_chdir(to);
    if (to_vnode == -1) {
        printf("Failed to get vnode for %s\n", to);
        return false;
    }
    
    uint64_t orig_to_v_data = kread64(to_vnode + 0xE0); // vnode->v_data
    
    uint64_t from_vnode = get_vnode_for_path_by_chdir(from);
    if (from_vnode == -1) {
        printf("Failed to get vnode for %s\n", from);
        return -1;
    }
    
    uint64_t from_v_data = kread64(from_vnode + 0xE0);
    
    kwrite64(to_vnode + 0xE0, from_v_data);
    
    vnode_chown(from_vnode, 501, 501);
    vnode_chown(to_vnode, 501, 501);
    usleep(2000);
    return orig_to_v_data;
}

bool vnode_unredirect_folder(const char *folder, uint64_t orig) {
    uint64_t vnode = get_vnode_for_path_by_chdir(folder);
    if (vnode == -1) { return false; }
    
    kwrite64(vnode + 0xE0, orig); // vnode->v_data
    
    return true;
}
