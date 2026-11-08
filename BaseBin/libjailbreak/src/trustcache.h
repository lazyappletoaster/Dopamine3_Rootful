#ifndef TRUSTCACHE_H
#define TRUSTCACHE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "trustcache_structs.h"
#include <xpc/xpc.h>

#define BASEBIN_TRUSTCACHE_UUID (uuid_t){'B','A','S','E','B','I','N','\0','\0','\0','\0','\0','\0','\0','\0','\0'}
#define DYLD_TRUSTCACHE_UUID (uuid_t){'D','Y','L','D','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0'}

int trustcache_list_insert(uint64_t tcKaddr);

// Whether the running kernel understands the version 2 trust cache format (iOS 16+ only)
bool trustcache_v2_supported(void);
// Size of a single entry in a trust cache file of the given format version
size_t trustcache_file_entry_size(uint32_t version);
// Total size of a trust cache file (header + entries), based on its format version
size_t trustcache_file_size(trustcache_file_v1 *tc);

// Converts a trust cache file to the version 2 format with every entry assigned constraint
// category 0 ("no constraints"), which turns LaunchConstraints off for all of its cdhashes.
// The file passed in is reallocated (the old buffer is freed) and replaces *tc on success.
int trustcache_file_disable_launch_constraints(trustcache_file_v1 **tc);

int jb_trustcache_add_entries(struct trustcache_entry_v1 *entries, uint32_t entryCount);
int jb_trustcache_add_entry(trustcache_entry_v1 entry);
int jb_trustcache_add_cdhashes(cdhash_t *hashes, uint32_t hashCount);
xpc_object_t jb_trustcache_info(void);
void jb_trustcache_clear(void);

void jb_trustcache_debug_print(FILE *f);

// Uploads a trust cache file, upgrading it to the version 2 format with LaunchConstraints
// disabled (constraint category 0) first whenever the running kernel supports that format
int trustcache_file_upload(trustcache_file_v1 *tc);
int trustcache_file_upload_with_uuid(trustcache_file_v1 *tc, uuid_t uuid);
int trustcache_file_build_from_cdhashes(cdhash_t *CDHashes, uint32_t CDHashCount, trustcache_file_v1 **tcOut);
int trustcache_file_build_from_path(const char *filePath, trustcache_file_v1 **tcOut);

bool is_cdhash_in_trustcache(uint64_t tcKaddr, cdhash_t CDHash);
bool is_cdhash_trustcached(cdhash_t CDHash);

#endif
