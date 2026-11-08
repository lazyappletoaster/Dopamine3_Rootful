//
//  main.m
//  makerw_apfs
//
//  Created by untether
//

#include <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <copyfile.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include "dirutils.h"
#include <sys/param.h>
#include "apfs_utils.h"
#include "vnode_utils.h"

/*
Idea:
1) Create new APFS partition
2) Mount it to /var/mnt/<name_of_volume>/
3) Copy staff recursively from provided path to /var/mnt/<name_of_volume>/
4) Unmount /var/mnt/<name_of_volume>/
5) Mount partition over provided path
Profit?
*/

void printf_error(char *format, ...) {
    printf("\x1b[1;31m");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\x1b[0m");
    return;
}

void printf_success(char* format, ...) {
    printf("\x1b[1;32m");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\x1b[0m");
    return;
}

#import <Foundation/Foundation.h>
#import <stdio.h>
#import <string.h>
#import <stdbool.h>
#import <stdlib.h>

char* walkPartitions(const char* volumeNameIn) {
    if (!volumeNameIn) return NULL;
    const char* devicveSample = "disk0s1s%d";
    if (@available(iOS 16.0, *)) {
        devicveSample = "disk1s%d";
    }
    for (int partNum = 1; ; partNum++) {
        char currentPartition[64];
        snprintf(currentPartition, sizeof(currentPartition), devicveSample, partNum);
        char* volumeName = getName(currentPartition);
        if (!volumeName) {
            debug("Reached the end of volumes, exiting\n");
            break; 
        }
        printf("%s -> %s\n", currentPartition, volumeName);
        if (strcmp(volumeNameIn, volumeName) == 0) {
            char* device = malloc(256);
            if (!device) {
                printf_error("[-] FAIL: failed to allocate memory at device detection.");
                exit(-1);
            }
            snprintf(device, 256, "/dev/%s", currentPartition);
            debug("Found partition: %s\n", device);
            return device; 
        }
    }
    return "";
}


// 1 -> prepared, 0 -> not prepared, -1 -> could not be read through the vnode
int check_partition(char* path) {
    if (!path) return -1;
    if (vnode_calls_init() != 0) return -1;

    char path_internal[512] = {0};
    snprintf(path_internal, sizeof(path_internal), "%s/.Dopamine_Rootful/fsPrepared", path);
    debug("Checking partition at path: %s\n", path_internal);

    int prepared = vnode_path_exists(path_internal) ? 1 : 0;
    debug(prepared ? "Partition ok\n" : "Partition corrupted\n");
    return prepared;
}

int prepare_dopamine_partition(char* path, char* volumeNameIn) {
    // path - where to mount, volumeNameIn - name of device to mount
    int ret;
    char device[256];
    char* ptr = walkPartitions(volumeNameIn);
    strcpy(device, ptr);

    char tempMount[256] = "/var/mnt/";
    char prepared_flag[512] = {0};
    
    strcat(tempMount, volumeNameIn);
    ret = ensure_directory_exists(tempMount);


    debug("preparing partition: %s\n", tempMount);
    debug("going to mount %s device over %s tempdir\n", ptr, tempMount);


    ret = mount_apfs(tempMount, 0, device);
    debug("tempdir APFS ret: %i\n", ret);

    strcpy(prepared_flag, tempMount);
    strcat(prepared_flag, "/.Dopamine_Rootful");
    debug(".Dopamine_Rootful at: %s\n", prepared_flag);

    ret = ensure_directory_exists(prepared_flag);

    strcat(prepared_flag, "/fsPrepared");
    debug("fsPrepared at: %s\n", prepared_flag);

    int fd = open(prepared_flag, O_RDWR | O_CREAT);
    dprintf(fd, "Hello untether");
    close(fd);

    // The flag lives on the new volume, stamping it through the vnode keeps its
    // ownership independent of the umask of whoever created it
    if (vnode_calls_init() == 0) {
        if (!vnode_write_attrs(prepared_flag, 0, 0, 0644)) {
            debug("Could not write the vnode attributes of %s\n", prepared_flag);
        }
    }

    ret = copy_dir_recursive(path, tempMount); // copy all direcory from <path> to tempMount
    debug("copy dir recursive returned %i\n", ret);

    return 0;
}


// Every failure is reported as a code, so a caller can tell the cases apart
typedef enum {
    BSFS_OK                      = 0,
    BSFS_ERR_NO_KERNEL_PRIMITIVE = 1,
    BSFS_ERR_NO_TARGET_VNODE     = 2,
    BSFS_ERR_NO_SOURCE_VNODE     = 3,
    BSFS_ERR_REDIRECT_FAILED     = 4,
    BSFS_ERR_ATTRIBUTES_FAILED   = 5,
} bsfs_error;

static bool bsfs_uses_vnode_overlay(void) {
    // iOS 16 and newer have no usable APFS mount path here, every read and write
    // has to go through the vnode. Older versions keep the volume based fallback.
    if (@available(iOS 16.0, *)) {
        return true;
    }
    return false;
}

// Points <path> at the bootstrap copy of the same path inside the jailbreak
// root: reads and writes only touch vnode data, nothing is copied and no path
// based syscall is involved
static int create_vnode_overlay(char *path, char *volumeNameIn) {
    (void)volumeNameIn;

    if (vnode_calls_init() != 0) {
        printf_error("[-] FAIL: no kernel r/w available for the vnode of %s\n", path);
        return BSFS_ERR_NO_KERNEL_PRIMITIVE;
    }

    char *jbrootPath = getItemInJBROOT(path);
    if (!jbrootPath || jbrootPath[0] == '\0') {
        printf_error("[-] FAIL: unable to resolve %s inside the jailbreak root\n", path);
        return BSFS_ERR_NO_SOURCE_VNODE;
    }

    uint64_t targetData = vnode_read_data_for_path(path);
    if (targetData == 0) {
        printf_error("[-] FAIL: no vnode for %s\n", path);
        return BSFS_ERR_NO_TARGET_VNODE;
    }

    uint64_t sourceData = vnode_read_data_for_path(jbrootPath);
    if (sourceData == 0) {
        printf_error("[-] FAIL: no vnode for %s\n", jbrootPath);
        return BSFS_ERR_NO_SOURCE_VNODE;
    }

    if (targetData == sourceData) {
        printf_success("[+] %s already reads and writes %s\n", path, jbrootPath);
        return BSFS_OK;
    }

    uint64_t origData = 0;
    if (vnode_redirect_folder_at_path(path, jbrootPath, 0, 0, 0755, &origData) != 0) {
        printf_error("[-] FAIL: unable to point %s at %s\n", path, jbrootPath);
        return BSFS_ERR_REDIRECT_FAILED;
    }

    printf_success("[+] %s now reads and writes %s\n", path, jbrootPath);
    return BSFS_OK;
}

static int create_apfs_partition_legacy(char* path, char* volumeNameIn) {
    // put code in here
    uint64_t credBackup = 0;
    int ret = 0;
    int loopcount = 0;
    char device[256];
    char* ptr = NULL;
    start:

    if (loopcount > 6) {
        printf_error("[-] FAIL: Something went wrong, deadloop detected\n");
        printf_error("[-] EXITING\n");
        return 1;
    } 
    ptr = walkPartitions(volumeNameIn);
    strcpy(device, ptr);
    // char->ptr->char wtf

    if (strcmp(device, "") != 0) { // did find correct partition
        printf("Mounting %s over %s\n", device, path);

        debug("Going to mount partition %s -> %s path\n", device, path);
        debug("MOUNT!\n");
        ret = mount_apfs(path, MNT_FORCE, device);
        if (ret == KERN_SUCCESS) {
            printf("[+] mount_apfs returned %i\n", ret);
        } else {
            printf("[-] FAIL: mount_apfs returned %i\n", ret);
        }
        // need to check, if partition has dopamine flags.
        int prepared = check_partition(path);

        if (prepared < 0) {
            printf_error("[-] FAIL: unable to read the vnode of %s, not touching the partition\n", path);
            return BSFS_ERR_NO_KERNEL_PRIMITIVE;
        }

        if (prepared == 0) {
            printf("Partition invalid, killing it\n");

            debug("Giving kernel privileges\n");
            jbclient_root_steal_ucred(0, &credBackup);
            ret = unmount(path, MNT_FORCE);
            jbclient_root_steal_ucred(credBackup, NULL);
            debug("Dropping kernel privileges\n");
            printf("[+] unmount ret %i\n", ret);

            ret = _APFSVolumeDelete(device);
            printf("[+] volume %s deleted, recreating\n", device);
            goto start;
        }

    } else {
        char* rootDiskDevice = "disk0s1";
        debug("No partition found with name %s -> calling _APFSVolumeCreate\n", volumeNameIn);
        if (@available(iOS 16.0, *)) {
            rootDiskDevice = "disk1";
        }
        debug("rootDiskDevice: %s\n", rootDiskDevice);
        NSDictionary *createDict = @{@"com.apple.apfs.volume.name": [[NSString alloc] initWithUTF8String:volumeNameIn]};

		CFMutableDictionaryRef createDictMut = CFDictionaryCreateMutableCopy(NULL, 0, (__bridge CFDictionaryRef)createDict);

        _APFSVolumeCreate(rootDiskDevice, createDictMut);


        int partitionPrepared = prepare_dopamine_partition(path, volumeNameIn);
        // 0. At this point we have a new empty APFS device in /dev. The strategy is:
        // 1. Mount it over /var/mnt/<volumeNameIn>
        // 2. Copy all from real <path> to /var/mnt/<volumeNameIn>
        // 3. unmount /var/mnt/<volumeNameIn>
        // 4. mount over real <path>

        // Do it right now
        printf("Partition prepared for %s, unmounting tempdir\n", volumeNameIn);
        char tempMount[256] = "/var/mnt/";
        strcat(tempMount, volumeNameIn);

        debug("Giving kernel privileges\n");
        jbclient_root_steal_ucred(0, &credBackup);
        ret = unmount(tempMount, MNT_FORCE);
        printf("[+] unmount ret %i\n", ret);
        jbclient_root_steal_ucred(credBackup, NULL);
        debug("Dropping kernel privileges\n");
        printf("[+] Remounting to real directory.\n");
        goto start;
    }
    return 0;
}

int create_apfs_partition(char* path, char* volumeNameIn) {
    if (bsfs_uses_vnode_overlay()) {
        // iOS 16 and newer: a failure is reported instead of being hidden behind
        // the volume based fallback
        return create_vnode_overlay(path, volumeNameIn);
    }

    return create_apfs_partition_legacy(path, volumeNameIn);
}

int main(int argc, char *argv[]) {
    int ret = 0;
    if (getuid() != 0) {
        printf("FAIL: run as root");
        return -1;
    }

    int calls = initialize_calls();
    debug("Calls initialized ret %i\n", calls);
    
    if (argc < 2) {
        printf_success("Usage:\n");
        printf("  %s create <path> <volName>\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            printf_error("[-] Missing path argument.\n");
            return 1;
        }
        if (argc < 4) {
            printf_error("[-] Missing volume name.\n");
            return 1;
        }
        const char *path = argv[2];
        const char *volumeNameIn = argv[3];
        ret = create_apfs_partition(path, volumeNameIn);
        if (ret == BSFS_OK) {
            printf_success("[+] %s is writable\n", path);
        }
        else {
            printf_error("[-] Preparing %s failed with error code %i\n", path, ret);
        }
    }
    else {
        printf_error("[-] Unknown command.\n");
    }

    return ret;
}