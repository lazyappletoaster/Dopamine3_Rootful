#ifndef TRUSTCACHE_STRUCTS_H
#define TRUSTCACHE_STRUCTS_H

#include <uuid/uuid.h>
#include <choma/CSBlob.h>
#include <stdint.h>
#include "signatures.h"

typedef uint8_t cdhash_t[CS_CDHASH_LEN];

typedef struct trustcache_entry_v1
{
	cdhash_t hash;
	uint8_t hash_type;
	uint8_t flags;
} __attribute__((__packed__)) trustcache_entry_v1;

typedef struct s_trustcache_file_v1
{
	uint32_t version;
	uuid_t uuid;
	uint32_t length;
	trustcache_entry_v1 entries[];
} __attribute__((__packed__)) trustcache_file_v1;

#define TC_FILE_VERSION_V1 1
#define TC_FILE_VERSION_V2 2

/* Version 2 trust caches (iOS 16+, introduced together with LaunchConstraints) add a per entry
 * "constraint category" to the file, which is what AMFI uses to enforce LaunchConstraints on a
 * binary. Category 0 means "no constraints", so every entry we add with category 0 launches
 * without any LaunchConstraints being enforced on it. */
#define TC_CONSTRAINT_CATEGORY_NONE 0

typedef struct trustcache_entry_v2
{
	cdhash_t hash;
	uint8_t hash_type;
	uint8_t flags;
	uint8_t constraint_category;
	uint8_t reserved;
} __attribute__((__packed__)) trustcache_entry_v2;

/* Version 2 files use the exact same header layout as version 1, only the entries grow */
typedef struct s_trustcache_file_v2
{
	uint32_t version;
	uuid_t uuid;
	uint32_t length;
	trustcache_entry_v2 entries[];
} __attribute__((__packed__)) trustcache_file_v2;

#define JB_MAGIC 0x424a424a424a424a // "JBJBJBJB"
typedef struct jb_trustcache
{
	// On iOS 15, the trustcache struct has a size of 0x10
	// On iOS 16, it has one of 0x28, we just have to make sure our field is bigger
	uint8_t trustcache[0x40];
	uint64_t magic;
	// Same header for both formats, the entries behind it are written in the format of the running kernel
	trustcache_file_v1 file;
} __attribute__((__packed__)) jb_trustcache;

#define JB_TRUSTCACHE_SIZE 0x4000
#define JB_TRUSTCACHE_MAX_ENTRY_SIZE sizeof(trustcache_entry_v2)
#define JB_TRUSTCACHE_ENTRY_COUNT ((JB_TRUSTCACHE_SIZE - sizeof(jb_trustcache)) / JB_TRUSTCACHE_MAX_ENTRY_SIZE)

#endif
