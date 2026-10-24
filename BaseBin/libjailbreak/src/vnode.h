//
//  vnode.h
//  Dopamine3_Rootful
//
//  Created by Alfie on 01/04/2024.
//

#ifndef VNODE_H
#define VNODE_H

#import <Foundation/Foundation.h>

uint64_t get_vnode_for_path(const char *path);
uint64_t get_vnode_for_path_by_chdir(const char *path);
uint64_t get_child_vnode_from_vnode(uint64_t vnode, const char *file);
bool vnode_chown(uint64_t vnode, uid_t uid, gid_t gid);
bool vnode_chmod(uint64_t vnode, mode_t mode);
uint64_t vnode_redirect_folder(const char *to, const char *from);
bool vnode_unredirect_folder(const char *folder, uint64_t orig);

#endif /* VNODE_H */
