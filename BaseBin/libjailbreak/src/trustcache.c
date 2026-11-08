#include "trustcache.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kernel.h"
#include "info.h"
#include "primitives.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(trustcache_entry_v2) == sizeof(trustcache_entry_v1) + 2, "version 2 trust cache entries must only add the constraint category and reserved bytes");
_Static_assert(sizeof(trustcache_file_v2) == sizeof(trustcache_file_v1), "version 1 and version 2 trust cache files must share the same header layout");
#endif

size_t trustcache_file_entry_size(uint32_t version)
{
	if (version == TC_FILE_VERSION_V2) {
		return sizeof(trustcache_entry_v2);
	}
	return sizeof(trustcache_entry_v1);
}

static bool _trustcache_file_version_is_supported(uint32_t version)
{
	return version == TC_FILE_VERSION_V1 || version == TC_FILE_VERSION_V2;
}

size_t trustcache_file_size(trustcache_file_v1 *tc)
{
	if (!tc || !_trustcache_file_version_is_supported(tc->version)) return 0;
	return sizeof(trustcache_file_v1) + (tc->length * trustcache_file_entry_size(tc->version));
}

bool trustcache_v2_supported(void)
{
	// LaunchConstraints and with them the version 2 trust cache format were introduced in iOS 16.
	// Kernels before that only understand version 1 modules, so they must not be handed a version 2 one.
	return ksymbol(ppl_trust_cache_rt) != 0 || ksymbol_txm(txm_trustcache_root) != 0;
}

static uint32_t _jb_trustcache_version(void)
{
	return trustcache_v2_supported() ? TC_FILE_VERSION_V2 : TC_FILE_VERSION_V1;
}

static void _trustcache_file_init(trustcache_file_v1 *file, uint32_t version)
{
	memset(file, 0, sizeof(*file));
	file->version = version;
	uuid_generate(file->uuid);
}

// iOS 16:
// ppl_trust_cache_rt has trustcache runtime
// **(ppl_trust_cache_rt+0x20) seems to have the loaded trustcache linked list
// trustcache struct changed, "next" is still at +0x0, but "this" is at +0x20

uint64_t _trustcache_list_get_start(void)
{
	if (ksymbol(pmap_image4_trust_caches)) { // iOS <=15
		return kread64(ksymbol(pmap_image4_trust_caches));
	}
	else if (ksymbol(ppl_trust_cache_rt)) {  // iOS >=16, PPL
		return kread64(kread64(ksymbol(ppl_trust_cache_rt) + 0x20));
	}
	else if (ksymbol_txm(txm_trustcache_root)) { // iOS >=17, SPTM/TXM
		return kread64(kread64(ksymbol_txm(txm_trustcache_root) + 0x20));
	}

	return 0;
}

void _trustcache_list_set_start(uint64_t newStart)
{
	if (ksymbol(pmap_image4_trust_caches)) { // iOS <=15
		kwrite64(ksymbol(pmap_image4_trust_caches), newStart);
	}
	else if (ksymbol(ppl_trust_cache_rt)) {  // iOS >=16
		kwrite64(kread64(ksymbol(ppl_trust_cache_rt) + 0x20), newStart);
	}
	else if (ksymbol_txm(txm_trustcache_root)) { // iOS >=17, SPTM/TXM
		kwrite64(kread64(ksymbol_txm(txm_trustcache_root) + 0x20), newStart);
	}
}

void _trustcache_list_enumerate(void (^enumerateBlock)(uint64_t tcKaddr, bool *stop))
{
	uint64_t curTC = _trustcache_list_get_start();
	while(curTC != 0) {
		bool stop = false;
		enumerateBlock(curTC, &stop);
		if (stop) break;
		curTC = kread64(curTC + koffsetof(trustcache, nextptr));
	}
}

int trustcache_list_insert(uint64_t tcToInsert)
{
	if (!tcToInsert) return -1;

	if (ksymbol(SPTMArgs)) {
		// On SPTM/TXM devices, our allocations are read-only by TXM so it cannot write to the prevptr field 
		// Since TXM will only add trust caches to the start of the list, we simply add ours to the end
		// We avoid a panic when loading a new trustcache, since we guarantee the first trustcache in the list is always writable

		__block uint64_t lastTC = 0;
		_trustcache_list_enumerate(^(uint64_t tcKaddr, bool *stop) {
			lastTC = tcKaddr;
		});

		if (koffsetof(trustcache, prevptr)) {
			kwrite64(tcToInsert + koffsetof(trustcache, prevptr), lastTC);
		}
		kwrite64(lastTC + koffsetof(trustcache, nextptr), tcToInsert);
	}
	else {
		uint64_t previousStartTC = _trustcache_list_get_start();
		kwrite64(tcToInsert + koffsetof(trustcache, nextptr), previousStartTC);
		if (koffsetof(trustcache, prevptr)) {
			kwrite64(previousStartTC + koffsetof(trustcache, prevptr), tcToInsert);
		}
		_trustcache_list_set_start(tcToInsert);
	}

	return 0;
}

int trustcache_list_remove(uint64_t tcKaddr)
{
	if (!tcKaddr) return -1;

	uint64_t nextTc = kread64(tcKaddr + koffsetof(trustcache, nextptr));

	uint64_t curTc = _trustcache_list_get_start();
	if (curTc == 0) {
		return -1;
	}
	else if (curTc == tcKaddr) {
		_trustcache_list_set_start(nextTc);
		if (nextTc && koffsetof(trustcache, prevptr)) {
			kwrite64(nextTc + koffsetof(trustcache, prevptr), 0);
		}
	}
	else {
		uint64_t prevTc = 0;
		while (curTc != tcKaddr)
		{
			if (curTc == 0) {
				return -1;
			}
			prevTc = curTc;
			curTc = kread64(curTc);
		}
		kwrite64(prevTc + koffsetof(trustcache, nextptr), nextTc);
		if (nextTc && koffsetof(trustcache, prevptr)) {
			kwrite64(nextTc + koffsetof(trustcache, prevptr), prevTc);
		}
	}

	return 0;
}

int _trustcache_file_sort_entry_comparator(const void *vp1, const void *vp2)
{
	// Both formats start every entry with the cdhash, so this works regardless of the entry size
	return memcmp(vp1, vp2, sizeof(cdhash_t));
}

void _trustcache_file_sort(trustcache_file_v1 *file)
{
	qsort(file->entries, file->length, trustcache_file_entry_size(file->version), _trustcache_file_sort_entry_comparator);
}

bool _is_jb_trustcache(uint64_t tcKaddr)
{
	uint64_t jbTcFile = tcKaddr + offsetof(jb_trustcache, file);
	uint64_t file = kread64(tcKaddr + koffsetof(trustcache, fileptr));
	if (file == jbTcFile) {
		// If there is exactly one 8-byte value between the kpage start and the trustcache file,
		// Check if that matches against the JB_MAGIC
		// This is a 100% accurate way of determining whether this entry is a jb_trustcache or not
		return (kread64(tcKaddr + offsetof(jb_trustcache, magic)) == JB_MAGIC);
	}
	return false;
}

void _jb_trustcache_enumerate(void (^enumerateBlock)(uint64_t jbTcKaddr, bool *stop))
{
	_trustcache_list_enumerate(^(uint64_t tcKaddr, bool *stop) {
		if (_is_jb_trustcache(tcKaddr)) {
			enumerateBlock(tcKaddr, stop);
		}
	});
}

void jb_trustcache_clear(void)
{
	_jb_trustcache_enumerate(^(uint64_t jbTcKaddr, bool *stop) {
		kwrite64(jbTcKaddr + offsetof(jb_trustcache, file.length), 0);
	});
}

uint64_t _jb_trustcache_grow(void)
{
	uint64_t jbTcKern = 0;
	if (kalloc(&jbTcKern, 0x4000) != 0) return 0;

	jb_trustcache *jbTc = alloca(sizeof(jb_trustcache));
	memset(jbTc, 0, sizeof(jb_trustcache));
	_trustcache_file_init(&jbTc->file, _jb_trustcache_version());
	jbTc->magic = JB_MAGIC;
	*(uint64_t *)(jbTc->trustcache + koffsetof(trustcache, fileptr)) = (jbTcKern + offsetof(jb_trustcache, file));
	if (koffsetof(trustcache, size)) {
		*(uint64_t *)(jbTc->trustcache + koffsetof(trustcache, size)) = JB_TRUSTCACHE_SIZE;
	}
	if (koffsetof(trustcache, type)) {
		*(uint64_t *)(jbTc->trustcache + koffsetof(trustcache, type)) = 0x5;
	}
	kwritebuf(jbTcKern, jbTc, sizeof(*jbTc));
	trustcache_list_insert(jbTcKern);
	return jbTcKern;
}

static uint32_t _jb_trustcache_entry_capacity(uint32_t version)
{
	size_t entriesOffset = offsetof(jb_trustcache, file) + offsetof(trustcache_file_v1, entries);
	return (JB_TRUSTCACHE_SIZE - entriesOffset) / trustcache_file_entry_size(version);
}

int jb_trustcache_add_entries(struct trustcache_entry_v1 *entries, uint32_t entryCount)
{
	uint32_t version = _jb_trustcache_version();
	size_t entrySize = trustcache_file_entry_size(version);
	uint32_t entryCapacity = _jb_trustcache_entry_capacity(version);

	uint32_t remainingEntryCount = entryCount;
	while (remainingEntryCount > 0) {
		__block uint64_t freeJbTcKaddr = 0;
		__block uint32_t freeJbTcCurrentLength = 0;
		_jb_trustcache_enumerate(^(uint64_t jbTcKaddr, bool *stop) {
			// Only append to modules that use the same entry format, mixing versions would corrupt them
			uint32_t moduleVersion = kread32(jbTcKaddr + offsetof(jb_trustcache, file.version));
			uint32_t length = kread32(jbTcKaddr + offsetof(jb_trustcache, file.length));
			if (moduleVersion == version && length < entryCapacity) {
				freeJbTcKaddr = jbTcKaddr;
				freeJbTcCurrentLength = length;
				*stop = true;
			}
		});
		if (freeJbTcKaddr == 0) {
			freeJbTcKaddr = _jb_trustcache_grow();
			if (freeJbTcKaddr == 0) return -1;
		}

		uint32_t entryCountToInsert = entryCapacity - freeJbTcCurrentLength;
		if (remainingEntryCount < entryCountToInsert) {
			entryCountToInsert = remainingEntryCount;
		}

		jb_trustcache *jbTc = alloca(JB_TRUSTCACHE_SIZE);
		kreadbuf(freeJbTcKaddr, jbTc, JB_TRUSTCACHE_SIZE);
		uint8_t *jbTcEntries = (uint8_t *)jbTc + offsetof(jb_trustcache, file) + offsetof(trustcache_file_v1, entries);
		for (uint32_t i = 0; i < entryCountToInsert; i++) {
			// Entries are written in the format of the running kernel, with the constraint category
			// set to "no constraints" so AMFI does not enforce any LaunchConstraints on them
			trustcache_entry_v2 entry = { 0 };
			memcpy(entry.hash, entries[i].hash, sizeof(cdhash_t));
			entry.hash_type = entries[i].hash_type;
			entry.flags = entries[i].flags;
			entry.constraint_category = TC_CONSTRAINT_CATEGORY_NONE;
			memcpy(jbTcEntries + ((freeJbTcCurrentLength + i) * entrySize), &entry, entrySize);
		}
		jbTc->file.length += entryCountToInsert;
		_trustcache_file_sort(&jbTc->file);
		kwritebuf(freeJbTcKaddr, jbTc, JB_TRUSTCACHE_SIZE);
		remainingEntryCount -= entryCountToInsert;
	}
	return 0;
}

int jb_trustcache_add_cdhashes(cdhash_t *hashes, uint32_t hashCount)
{
	struct trustcache_entry_v1 entries[hashCount];
	for (int i = 0; i < hashCount; i++) {
		memcpy(entries[i].hash, hashes[i], sizeof(cdhash_t));
		entries[i].hash_type = 1;
		entries[i].flags = 0;
	}
	return jb_trustcache_add_entries(entries, hashCount);
}

int jb_trustcache_add_entry(struct trustcache_entry_v1 entry)
{
	return jb_trustcache_add_entries(&entry, 1);
}


/*int jb_trustcache_add_file(const char *filePath)
{
	
}

int jb_trustcache_add_directory(const char *directoryPath)
{

}*/

xpc_object_t jb_trustcache_info(void)
{
	xpc_object_t arr = xpc_array_create_empty();
	_jb_trustcache_enumerate(^(uint64_t jbTcKaddr, bool *stop) {
		uuid_t uuid;
		kreadbuf(jbTcKaddr + offsetof(jb_trustcache, file.uuid), (void *)uuid, sizeof(uuid));
		uint32_t length = kread32(jbTcKaddr + offsetof(jb_trustcache, file.length));

		xpc_object_t tcDict = xpc_dictionary_create_empty();
		xpc_dictionary_set_data(tcDict, "uuid", &uuid, sizeof(uuid));

		uint32_t version = kread32(jbTcKaddr + offsetof(jb_trustcache, file.version));
		size_t entrySize = trustcache_file_entry_size(version);
		uint64_t entriesKaddr = jbTcKaddr + offsetof(jb_trustcache, file) + offsetof(trustcache_file_v1, entries);

		xpc_object_t hashesArr = xpc_array_create_empty();
		for (uint32_t i = 0; i < length; i++) {
			cdhash_t hash;
			kreadbuf(entriesKaddr + (i * entrySize), hash, sizeof(cdhash_t));
			xpc_array_set_data(hashesArr, XPC_ARRAY_APPEND, hash, sizeof(cdhash_t));
		}
		xpc_dictionary_set_value(tcDict, "cdhashes", hashesArr);
		xpc_release(hashesArr);

		xpc_array_append_value(arr, tcDict);
		xpc_release(tcDict);
	});
	return arr;
}

void jb_trustcache_debug_print(FILE *f)
{
	__block int i = 0;
	_jb_trustcache_enumerate(^(uint64_t jbTcKaddr, bool *stop) {
		uuid_t uuid;
		kreadbuf(jbTcKaddr + offsetof(jb_trustcache, file.uuid), (void *)uuid, sizeof(uuid));
		uint32_t length = kread32(jbTcKaddr + offsetof(jb_trustcache, file.length));

		uint32_t *uuidData = (uint32_t *)uuid;
		uint32_t version = kread32(jbTcKaddr + offsetof(jb_trustcache, file.version));
		size_t entrySize = trustcache_file_entry_size(version);
		uint64_t entriesKaddr = jbTcKaddr + offsetof(jb_trustcache, file) + offsetof(trustcache_file_v1, entries);

		fprintf(f, "Jailbreak TrustCache %d <%08x%08x%08x%08x> (version: %u) (length: %u) (kaddr: 0x%llx):\n", i++, htonl(uuidData[0]), htonl(uuidData[1]), htonl(uuidData[2]), htonl(uuidData[3]), version, length, jbTcKaddr);
		
		for (uint32_t j = 0; j < length; j++) {
			cdhash_t hash;
			kreadbuf(entriesKaddr + (j * entrySize), hash, sizeof(cdhash_t));
			fprintf(f, "| ");
			for (uint32_t k = 0; k < sizeof(cdhash_t); k++) {
				fprintf(f, "%02x", hash[k]);
			}
			fprintf(f, "\n");
		}
	});
}

// Copies a trust cache file into the version 2 format with every entry assigned constraint
// category 0 ("no constraints"). This is what disables LaunchConstraints for the cdhashes in it,
// including for cdhashes that are also covered by a static trust cache with an assigned category.
// Returns a malloc'd file the caller owns, or NULL if the input format is unknown.
static trustcache_file_v1 *_trustcache_file_copy_without_constraints(trustcache_file_v1 *tc)
{
	if (!tc || !_trustcache_file_version_is_supported(tc->version)) return NULL;

	size_t entrySize = trustcache_file_entry_size(TC_FILE_VERSION_V2);
	size_t newSize = sizeof(trustcache_file_v2) + (tc->length * entrySize);
	trustcache_file_v2 *tcNoConstraints = malloc(newSize);
	if (!tcNoConstraints) return NULL;
	memset(tcNoConstraints, 0, newSize);

	tcNoConstraints->version = TC_FILE_VERSION_V2;
	tcNoConstraints->length = tc->length;
	memcpy(tcNoConstraints->uuid, tc->uuid, sizeof(uuid_t));

	for (uint32_t i = 0; i < tc->length; i++) {
		trustcache_entry_v2 *dstEntry = &tcNoConstraints->entries[i];
		if (tc->version == TC_FILE_VERSION_V2) {
			memcpy(dstEntry, &((trustcache_file_v2 *)tc)->entries[i], sizeof(*dstEntry));
		}
		else {
			memcpy(dstEntry->hash, tc->entries[i].hash, sizeof(cdhash_t));
			dstEntry->hash_type = tc->entries[i].hash_type;
			dstEntry->flags = tc->entries[i].flags;
		}
		dstEntry->constraint_category = TC_CONSTRAINT_CATEGORY_NONE;
		dstEntry->reserved = 0;
	}

	return (trustcache_file_v1 *)tcNoConstraints;
}

int trustcache_file_disable_launch_constraints(trustcache_file_v1 **tcInOut)
{
	if (!tcInOut || !*tcInOut) return -1;

	trustcache_file_v1 *tcNoConstraints = _trustcache_file_copy_without_constraints(*tcInOut);
	if (!tcNoConstraints) return -1;

	free(*tcInOut);
	*tcInOut = tcNoConstraints;
	return 0;
}

int trustcache_file_upload(trustcache_file_v1 *tc)
{
	if (!tc || !_trustcache_file_version_is_supported(tc->version)) return -1;

	// Everything we upload should have LaunchConstraints turned off, so whenever the running kernel
	// understands the version 2 format we upgrade the file and assign every entry category 0.
	// Older kernels only know version 1, on those the file is uploaded unchanged (and a version 2
	// file is rejected rather than handed to a kernel that cannot parse it).
	trustcache_file_v1 *noConstraintsTc = NULL;
	if (trustcache_v2_supported()) {
		noConstraintsTc = _trustcache_file_copy_without_constraints(tc);
	}
	trustcache_file_v1 *tcToUpload = noConstraintsTc ? noConstraintsTc : tc;

	if (tcToUpload->version == TC_FILE_VERSION_V2 && !trustcache_v2_supported()) {
		free(noConstraintsTc);
		return -1;
	}

	uint64_t tcSize = ksizeof(trustcache) + trustcache_file_size(tcToUpload);
	if (tcSize > 0x4000) {
		free(noConstraintsTc);
		return -1;
	}

	// Check if there is already a TrustCache with the same UUID
	__block uint64_t existingTcKaddr = 0;
	_trustcache_list_enumerate(^(uint64_t tcKaddr, bool *stop) {
		uint64_t tcFileKaddr = kread64(tcKaddr + koffsetof(trustcache, fileptr));
		uuid_t tcFileUUID;
		kreadbuf(tcFileKaddr + offsetof(trustcache_file_v1, uuid), tcFileUUID, sizeof(tcFileUUID));
		if (memcmp(tcFileUUID, tcToUpload->uuid, sizeof(tcFileUUID)) == 0) {
			existingTcKaddr = tcKaddr;
			*stop = true;
		}
	});

	// If so, we want to either replace it or remove it
	if (existingTcKaddr != 0) {
		if (_is_jb_trustcache(existingTcKaddr)) {
			// There is something terribly wrong, abort
			free(noConstraintsTc);
			return -1;
		}

		uint64_t prevTcFile = kread64(existingTcKaddr + koffsetof(trustcache, fileptr));
		uint32_t prevTcVersion = kread32(prevTcFile + offsetof(trustcache_file_v1, version));
		uint32_t prevTcLength = kread32(prevTcFile + offsetof(trustcache_file_v1, length));
		uint64_t prevTcSize = ksizeof(trustcache) + sizeof(trustcache_file_v1) + (prevTcLength * trustcache_file_entry_size(prevTcVersion));
		if (prevTcSize == tcSize) {
			// If size is the same this is simple, just replace the file data
			kwritebuf(prevTcFile, tcToUpload, tcSize - ksizeof(trustcache));
			free(noConstraintsTc);
			return 0;
		}
		else {
			// If not, it gets more complicated and hacky...
			// We can't take a lock (at least not yet??) to ensure nothing accesses the original TrustCache after we freed it
			// So we do the next best thing, we remove it from the linked list and wait a bit before freeing it, hoping that any
			// outstanding reads on the memory would be done by then
			if (trustcache_list_remove(existingTcKaddr) != 0) {
				free(noConstraintsTc);
				return -1; // really unlikely error, if this triggers the world is probably upside down
			}
			usleep(10000); // hope for current accesses to finish if there are any (new accesses won't come as we removed the list entry)
			kfree(existingTcKaddr, prevTcSize); // free the original allocation
			// now just fall through and make this function add the new TrustCache
		}
	}

	uint64_t tcKaddr = 0;
	if (kalloc(&tcKaddr, tcSize) != 0) {
		free(noConstraintsTc);
		return -1;
	}

	// kalloc is not guaranteed to be zeroed, make sure trustcache head is zeroed
	char nullBuf[ksizeof(trustcache)];
	memset(nullBuf, 0, sizeof(nullBuf));
	kwritebuf(tcKaddr, nullBuf, sizeof(nullBuf));

	uint64_t tcFileKaddr = tcKaddr + ksizeof(trustcache);
	kwritebuf(tcFileKaddr, tcToUpload, tcSize - ksizeof(trustcache));

	kwrite64(tcKaddr + koffsetof(trustcache, fileptr), tcFileKaddr);
	if (koffsetof(trustcache, size)) {
		kwrite64(tcKaddr + koffsetof(trustcache, size), tcSize);
	}
	if (koffsetof(trustcache, type)) {
		kwrite64(tcKaddr + koffsetof(trustcache, type), 0x5);
	}

	trustcache_list_insert(tcKaddr);
	free(noConstraintsTc);
	return 0;
}

int trustcache_file_upload_with_uuid(trustcache_file_v1 *tc, uuid_t uuid)
{
	memcpy(tc->uuid, uuid, sizeof(uuid_t));
	return trustcache_file_upload(tc);
}

int trustcache_file_build_from_cdhashes(cdhash_t *CDHashes, uint32_t CDHashCount, trustcache_file_v1 **tcOut)
{
	if (!CDHashes || CDHashCount == 0 || !tcOut) return -1;

	size_t tcSize = sizeof(trustcache_file_v1) + (sizeof(trustcache_entry_v1) * CDHashCount);
	trustcache_file_v1 *file = malloc(tcSize);
	_trustcache_file_init(file, TC_FILE_VERSION_V1);

	file->length = CDHashCount;
	for (uint32_t i = 0; i < CDHashCount; i++) {
		memcpy(file->entries[i].hash, CDHashes[i], sizeof(cdhash_t));
		file->entries[i].hash_type = 2;
		file->entries[i].flags = 0;
	}
	_trustcache_file_sort(file);

	*tcOut = file;
	return 0;
}

int trustcache_file_build_from_path(const char *filePath, trustcache_file_v1 **tcOut)
{
	int fd = open(filePath, O_RDONLY);
	struct stat s = { 0 };
	fstat(fd, &s);
	
	size_t tcSize = s.st_size;
	if (tcSize < (sizeof(trustcache_file_v1))) {
		// To small to be a TrustCache, file is probably malformed
		return -1;
	}

	trustcache_file_v1 *file = malloc(tcSize);
	read(fd, file, tcSize);
	close(fd);

	if (!_trustcache_file_version_is_supported(file->version)) {
		// Unknown TrustCache version, we do not know how big its entries are
		free(file);
		return -1;
	}

	size_t actualTcSize = sizeof(trustcache_file_v1) + (trustcache_file_entry_size(file->version) * file->length);
	if (actualTcSize != tcSize) {
		// Size mismatch, file is malformed
		free(file);
		return -1;
	}

	*tcOut = file;
	return 0;
}

bool trustcache_contains_cdhash(uint64_t tcKaddr, cdhash_t CDHash)
{
	uint64_t tcFileKaddr = kread64(tcKaddr + koffsetof(trustcache, fileptr));
	uint32_t version = kread32(tcFileKaddr + offsetof(trustcache_file_v1, version));
	uint32_t length = kread32(tcFileKaddr + offsetof(trustcache_file_v1, length));
	if (length == 0) return false;

	// Version 2 entries are larger, so the stride between entries depends on the file version
	size_t entrySize = trustcache_file_entry_size(version);
	uint64_t entriesKaddr = tcFileKaddr + offsetof(trustcache_file_v1, entries);

	int32_t left = 0;
	int32_t right = length - 1;

	while (left <= right) {
		int32_t mid = (left + right) / 2;
		cdhash_t itCDHash;
		kreadbuf(entriesKaddr + (mid * entrySize), itCDHash, CS_CDHASH_LEN);
		int32_t cmp = memcmp(CDHash, itCDHash, CS_CDHASH_LEN);
		if (cmp == 0) {
			return true;
		}
		if (cmp < 0) {
			right = mid - 1;
		} else {
			left = mid + 1;
		}
	}
	return false;
}

bool is_cdhash_trustcached(cdhash_t CDHash)
{
	__block bool inTrustCache = false;
	_trustcache_list_enumerate(^(uint64_t tcKaddr, bool *stop) {
		bool inThisTrustCache = trustcache_contains_cdhash(tcKaddr, CDHash);
		if (inThisTrustCache) {
			inTrustCache = true;
			*stop = true;
		}
	});
	return inTrustCache;
}
