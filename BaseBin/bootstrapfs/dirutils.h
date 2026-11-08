kern_return_t ensure_directory_exists(const char *path);
bool dir_exists_and_nonempty(const char *dir);
kern_return_t copy_dir_recursive(const char *src, const char *dst);
void debug(char *format, ...);
char* jbrootpath();
char* getItemInJBROOT(char* item);
// Defined once in apfs_utils.m, declared here so every file that includes this
// header does not emit its own copy of the symbol
extern int (*jbclient_root_steal_ucred)(uint64_t ucredToSteal, uint64_t *origUcred);