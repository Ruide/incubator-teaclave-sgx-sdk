#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
// #include <sys/time.h>
#include <time.h>
#include <stdarg.h> // for variable arguments functions
#include <fcntl.h>
#include <stdlib.h>
#include <sgx_tprotected_fs.h>

// At this point we have already definitions needed for ocall interface, so:
#define DO_NOT_REDEFINE_FOR_OCALL
#include "Enclave_t.h"

// For open64 need to define this
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define ESGX 0xffff /* SGX ERROR*/

/* intel sgx sdk 1.9 */
// extern SGX_FILE* SGXAPI sgx_fopen(const char* filename, const char* mode, const sgx_key_128bit_t *key);
// extern size_t SGXAPI sgx_fwrite(const void* ptr, size_t size, size_t count, SGX_FILE* stream);
// extern size_t SGXAPI sgx_fread(void* ptr, size_t size, size_t count, SGX_FILE* stream);
// extern int64_t SGXAPI sgx_ftell(SGX_FILE* stream);
// extern int32_t SGXAPI sgx_fseek(SGX_FILE* stream, int64_t offset, int origin);   
// extern int32_t SGXAPI sgx_fflush(SGX_FILE* stream);
// extern int32_t SGXAPI sgx_ferror(SGX_FILE* stream);
// extern int32_t SGXAPI sgx_fclose(SGX_FILE* stream);
// extern int32_t SGXAPI sgx_fclear_cache(SGX_FILE* stream);

// libc global error location
extern int* __errno_location(void);

// set global errno
void set_errno(int e){
  *(__errno_location()) = e;
}

int get_errno(){
  return *(__errno_location());
}

SGX_FILE* global_file_stream;

// The only call to sysconf is sysconf(_SC_PAGESIZE)
// hard code the value here
long int sysconf(int name){
   #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with name %d\n", __func__, name);
  ocall_print_string(msg);
  #endif 
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// replace by sgx_file::u_open64_ocall, maybe sgx_fopen_auto_key
int open64(const char *filename, int flags, ...){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with filename %s; flags %d\n", __func__, filename, flags);
  ocall_print_string(msg);
  #endif

  // #define	__S_ISUID	04000	/* Set user ID on execution.  */
  // #define	__S_ISGID	02000	/* Set group ID on execution.  */
  // #define	__S_ISVTX	01000	/* Save swapped text after use (sticky).  */
  // #define	__S_IREAD	0400	/* Read by owner.  */
  // #define	__S_IWRITE	0200	/* Write by owner.  */
  // #define	__S_IEXEC	0100	/* Execute by owner.  */

  // # define S_IROTH	(S_IRGRP >> 3)  /* Read by others.  */
  // # define S_IWOTH	(S_IWGRP >> 3)  /* Write by others.  */
  // # define S_IXOTH	(S_IXGRP >> 3)  /* Execute by others.  */
  // # define S_IRGRP	(S_IRUSR >> 3)  /* Read by group.  */
  // # define S_IWGRP	(S_IWUSR >> 3)  /* Write by group.  */
  // # define S_IXGRP	(S_IXUSR >> 3)  /* Execute by group.  */
  // /* Read, write, and execute by group.  */
  // # define S_IRWXG	(S_IRWXU >> 3)
  // mode_t mode = 0; // file permission bitmask
	// mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  
	// now open the file
	// read_only = (open_mode.read == 1 && open_mode.update == 0); // read only files can be opened simultaneously by many enclaves
	// fd = open(filename,	O_CREAT | (read_only ? O_RDONLY : O_RDWR) | O_LARGEFILE, mode); // create the file if it doesn't exists, read-only/read-write
  


  // `”w”` will create an empty file for writing. If the file already exists the existing file will be deleted or erased and the new empty file will be used. Be cautious while using these options.
  // `”w+”` will create an empty file for both reading and writing.
  
  const char* mode = "w";

  uint8_t key[16] = {1};
  global_file_stream = sgx_fopen(filename, mode, key); // fopen is compatible with c++ fopen, however, its semantics differs from os syscall open64
  int ret;
  int err;

  mode_t mode2 = 0; // file permission bitmask

  // Get the mode from arguments
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list valist;
		va_start(valist, flags); // Init a variable argument list
		mode = va_arg(valist, mode_t); // Retrieve next value with specified type
		va_end(valist); // End using variable argument list
	}

  // sgx_status_t status = ocall_open64(&ret, filename, flags, mode2);
  sgx_status_t status = u_open64_ocall(&ret, &err, filename, flags, mode2);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }

  err = get_errno();

  // errno 2 for ENOENT:	No such file or directory. fopen would open() file and put it in protected_fs_file class instance and close(fd). 
  // I guess fclose finalize encrypted content and write out.
  // if (err == 0 || err == 2) {
  if (err == 0) {
    set_errno(0);
    // return non-zero fd, however, with fopen, we donot have a fd. use 4 (0~3 reserved for stdin, stdout, etc).
    // we cannot emulate, because fcntl, fstat also requires emulate then.
    return ret;  
  } else {
    return -1;
  }
}

// replace by sgx_file::u_lseek_ocall, maybe sgx_fseek
off_t lseek64(int fd, off_t offset, int whence){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; offset %d; whence %d\n", __func__, fd, offset, whence);
  ocall_print_string(msg);
  #endif
  off_t ret;
  int err;
  
  sgx_status_t status = u_lseek64_ocall(&ret, &err, fd, offset, whence);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }
  return ret;
}

// since timezone is a "input" here, and it's "untrusted" (we don't make the
// decision). we can replace it with sgx_time.edl::u_clock_gettime_ocall.
int gettimeofday(struct timeval *tv, struct timezone *tz){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// try to replace with sgx_thread::u_nanosleep_ocall
unsigned int sleep(unsigned int seconds){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// do something like sqlite.c:931 disable dlopen by defining
// SQLITE_OMIT_LOAD_EXTENSION in CFLAGS
void *dlopen(const char *filename, int flag){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return NULL;
}

// do something like sqlite.c:931 disable dlopen by defining
// SQLITE_OMIT_LOAD_EXTENSION in CFLAGS
char *dlerror(void){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return NULL;
}

// do something like sqlite.c:931 disable dlopen by defining
// SQLITE_OMIT_LOAD_EXTENSION in CFLAGS
void *dlsym(void *handle, const char *symbol){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
}

// do something like sqlite.c:931 disable dlopen by defining
// SQLITE_OMIT_LOAD_EXTENSION in CFLAGS
int dlclose(void *handle){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

int utimes(const char *filename, const struct timeval times[2]){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// try using rtc_time64_to_tm to re-implement it
// timestamp from sgx_time.edl::u_clock_gettime_ocall with CLOCK_REALTIME
struct tm *localtime(const time_t *timep){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return NULL;
}

pid_t getpid(void){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s\n", __func__);
  ocall_print_string(msg);
  #endif
  // int ret;
  // sgx_status_t status = u_getpid_ocall(&ret);
  // if (status != SGX_SUCCESS) {
  //   char error_msg[256];
  //   snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
  //   ocall_print_error(error_msg);
  // }
  // return ret;
  return 0;
}

// fsync ask os to flush a file descriptor to disk. should be safe to directly relay.
int fsync(int fd){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d\n", __func__, fd);
  ocall_print_string(msg);
  #endif  
  int ret;
  int err;
  sgx_status_t status = u_fsync_ocall(&ret, &err, fd);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

// Define SQLITE_OMIT_RANDOMNESS to make sqlite no longer depends on time()
time_t time(time_t *t){
  time_t ret;
  
  memset(&ret, 0x0, sizeof(ret));
  
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  
  return ret;
}

// replace with u_close_ocall, maybe sgx_fclose
int close(int fd){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d\n", __func__, fd);
  ocall_print_string(msg);
  #endif
  int ret;
  int err;

  // this is different from close(int fd), and normally fopen should use close() but not fclose.
  // ret = sgx_fclose(global_file_stream);
  sgx_status_t status = u_close_ocall(&ret, &err, fd);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;

  // err = get_errno();
  // return ret;
}

// needs further discussion on access
int access(const char *pathname, int mode){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with pathname %s; mode %d\n", __func__, pathname, mode);
  ocall_print_string(msg);
  #endif
  int ret = 0;
  
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  
  return ret;
}

// replace with u_getcwd_ocall
char *getcwd(char *buf, size_t size){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s\n", __func__);
  ocall_print_string(msg);
  #endif
  char* ret;
  int err;
  sgx_status_t status = u_getcwd_ocall(&ret, &err, buf, size);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

// u_lstat_ocall or u_lstat64_ocall
int sgx_lstat( const char* path, struct stat *buf ) {
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with path %s\n", __func__, path);
  ocall_print_string(msg);
  #endif
  int ret;
  int err;
  sgx_status_t status = u_lstat_ocall(&ret, &err, path, buf);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

int sgx_stat(const char *path, struct stat *buf){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with path %s\n", __func__, path);
  ocall_print_string(msg);
  #endif
  int ret;
  int err;
  sgx_status_t status = u_stat_ocall(&ret, &err, path, buf);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

// u_fstat_ocall or u_fstat64_ocall
int sgx_fstat(int fd, struct stat *buf){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d\n", __func__, fd);
  ocall_print_string(msg);
  #endif

  int ret;
  int err;
  sgx_status_t status = u_fstat_ocall(&ret, &err, fd, buf);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

// u_ftruncate_ocall or u_ftruncate_64
int sgx_ftruncate(int fd, off_t length){
  int ret;
  int err;
  sgx_status_t status = u_ftruncate_ocall(&ret, &err, fd, length);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (ret == -1) {
    set_errno(err);
  }
  return ret;
}

// needs concretization
// if fd + cmd, no additional arg, use u_fcntl_arg0_ocall
// if fd + cmd + arg1, use u_fcntl_arg1_ocall
// grep osFcntl in sqlite3.c can get all call sites
int fcntl(int fd, int cmd, ... /* arg */ ){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; cmd %d\n", __func__, fd, cmd);
  ocall_print_string(msg);
  #endif

  // fcntl is only used for locking .db file. cmd == 6 for SETLK. we return 0 to notice that record lock has been set
  // but it is actually not. we assume no other process will read this .db file.
  return 0;

  // Read one argument
  // va_list valist;
	// va_start(valist, cmd);
	// void* arg = va_arg(valist, void*);
	// va_end(valist);

  // sgx_status_t status;
  // int ret;
  // int err;
  // status = u_fcntl_arg1_ptr_ocall(&ret, &err, fd, cmd, arg); 
  // if (status != SGX_SUCCESS) {
  //   char error_msg[256];
  //   snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
  //   ocall_print_error(error_msg);
  //   set_errno(ESGX);
  //   return -1;
  // }
  // if (ret == -1) {
  //   char error_msg[256];
  //   snprintf(error_msg, sizeof(error_msg), "%s%s return -1 with errno %d during cmd %d", "Debug: when calling ocall_", __func__, err, cmd);
  //   ocall_print_error(error_msg); 
  //   set_errno(err);
  // }
  // return ret;
}

// u_read_ocall, maybe sgx_fread
ssize_t read(int fd, void *buf, size_t count){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %d\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif 
  int ret;
  int err;
  sgx_status_t status = u_read_ocall(&ret, &err, fd, buf, count);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }
  return (ssize_t)ret;
}

// u_write_ocall, maybe sgx_fwrite
ssize_t write(int fd, const void *buf, size_t count){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %d\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif 
  int ret;
  int err;
  sgx_status_t status = u_write_ocall(&ret, &err, fd, buf, count);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }
  return (ssize_t)ret;
}

// u_fchmod_ocall
int fchmod(int fd, mode_t mode){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; mode %d\n", __func__, fd, mode);
  ocall_print_string(msg);
  #endif
  int ret;
  int err;
  sgx_status_t status = u_fchmod_ocall(&ret, &err, fd, mode);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }
  return (ssize_t)ret;
}

// u_unlink_ocall
int unlink(const char *pathname){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with pathname %s\n", __func__, pathname);
  ocall_print_string(msg);
  #endif
  int ret;
  int err;
  sgx_status_t status = u_unlink_ocall(&ret, &err, pathname);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }
  return ret;
}

// u_mkdir_ocall
int mkdir(const char *pathname, mode_t mode) {
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// u_rmdir_ocall
int rmdir(const char *pathname){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// disable by HAVE_FCHOWN=0
int fchown(int fd, uid_t owner, gid_t group){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// disable by HAVE_FCHOWN=0
uid_t geteuid(void){
  uid_t ret = 0;

  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);

  return ret;
}

// only 2 call-site to this function. all in unixTempFileDir
// we can
// 1. use u_getenv_ocall to reimplement, or
// 2. simply hard-code it here, or
// 3. make it configurable in some initializers which exposes rust API
char* getenv(const char *name){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with name %s\n", __func__, name);
  ocall_print_string(msg);
  #endif
  char* ret = NULL;
  sgx_status_t status = u_getenv_ocall(&ret, name);
  ocall_print_string("getenv called. name is: ");
  ocall_print_string(name);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
    ocall_print_error(error_msg);
  }
  return ret;
}

// Refer to sqlite3.c:14136
// Disable by setting SQLITE_MAX_MMAP_SIZE=0, or disable by line 14138
void *mmap64(void *addr, size_t len, int prot, int flags, int fildes, off_t off){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
}

// Refer to sqlite3.c:14136
// Disable by setting SQLITE_MAX_MMAP_SIZE=0, or disable by line 14138
int munmap(void *addr, size_t length){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}

// Refer to sqlite3.c:14136
// Disable by setting SQLITE_MAX_MMAP_SIZE=0, or disable by line 14138
void *mremap(void *old_address, size_t old_size, size_t new_size, int flags, ... /* void *new_address */){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
}

// replace with sgx_file::u_readlink_ocall
ssize_t readlink(const char *path, char *buf, size_t bufsiz){
  char error_msg[256];
  snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: no ocall implementation for ", __func__);
  ocall_print_error(error_msg);
  return 0;
}


int callback(void *NotUsed, int argc, char **argv, char **azColName){
    int i;
    for (i=0; i < argc; i++){
        ocall_print_string(azColName[i]);
        ocall_print_string(" = ");
        ocall_print_string(argv[i]);
        ocall_print_string("\n"); 
    }
    ocall_print_string("\n");
    return 0;
}
