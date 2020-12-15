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
#include <errno.h>

// At this point we have already definitions needed for ocall interface, so:
#define DO_NOT_REDEFINE_FOR_OCALL
#include "Enclave_t.h"

#define ESGX 0xffff /* SGX ERROR*/
#define MAX_FD_STREAM_ARRAY_SIZE 0x14 // #define FOPEN_MAX     20
#define FILE_NAME_SIZE 0xff // #define FILENAME_MAX  260. so 0xff = 256 < 260 is fine

// libc global error location
extern int* __errno_location(void);

// set global errno
void set_errno(int e){
  *(__errno_location()) = e;
}

int get_errno(){
  return *(__errno_location());
}

struct Fd_stream {
  SGX_FILE* file_stream;
  char file_name[FILE_NAME_SIZE];
  int fd; // zero initialized by-default
  int isUsed; // indicates if this item is set or not, 0 for notUsed (false), 1 for isUsed (true)
  int isDirectory; // 0 for no, 1 for yes
};

typedef struct Fd_stream Fd_stream;

Fd_stream global_fd_stream_array[MAX_FD_STREAM_ARRAY_SIZE];

sgx_key_128bit_t key = {1};
int global_fd = 3;

/* 
 * printf: 
 *   Invokes OCALL to display the enclave buffer to the terminal.
 */
int printf(const char* fmt, ...)
{
    char buf[BUFSIZ] = { '\0' };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
    return (int)strnlen(buf, BUFSIZ - 1) + 1;
}


void print_buf_hex(char* buf, int count){
  for (int i = 0; i < count; i++) {
    printf("%d", buf[i]);
  }
}
 
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


int sgx_open(const char *filename, int flags, ...){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with filename %s; flags %d\n", __func__, filename, flags);
  ocall_print_string(msg);
  #endif

  int fd = global_fd;
  global_fd += 1; // everytime open a new fd, never recycle, maybe impl recycle later

  // exist valid fd 
  const char* mode = "r+"; // read the file
  int fd_stream_idx = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    // check if stream exists
    if (global_fd_stream_array[fd_stream_idx].isUsed == 1){ 
      continue;
    }
    
    memcpy(global_fd_stream_array[fd_stream_idx].file_name , filename, FILE_NAME_SIZE);
    global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(filename, mode, key); 

    int ferror_code = sgx_ferror(global_fd_stream_array[fd_stream_idx].file_stream);
    int errno_code = get_errno();

    if (global_fd_stream_array[fd_stream_idx].file_stream == NULL) {
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Warning: Entering ocall_%s with filename %s; ferror code %d; errno code %d\n", __func__, global_fd_stream_array[fd_stream_idx].file_name, ferror_code, errno_code);
      ocall_print_string(msg);
      #endif
      if (errno_code == 2){ // 2	ENOENT	No such file or directory
        #if defined(SGX_OCALL_DEBUG)
        printf("%s", "Debug: sgx_open file not exsist, so use w+ to create one\n");
        #endif
        mode = "w+"; // w is write-only, can't read. w+ overwrite and can read/write
        global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(global_fd_stream_array[fd_stream_idx].file_name, mode, key); 
        
        #if defined(SGX_OCALL_DEBUG)
        errno_code = get_errno();
        printf("%s%d\n", "Debug: Errno is: ", errno_code);
        #endif
        set_errno(0);
        #if defined(SGX_OCALL_DEBUG)
        errno_code = get_errno();
        printf("%s%d\n", "Debug: Errno set to: ", errno_code);
        #endif
      }

      if (errno_code == 21){ // 21	EISDIR	Is a directory
        #if defined(SGX_OCALL_DEBUG)
        printf("%s", "Debug: sgx_open EISDIR Is a directory, and sgx_fopen does not support that, so fake one\n");
        #endif
        char sgx_filename[FILE_NAME_SIZE];
        for (int i =0; i < FILENAME_MAX - 5; i++) {
          sgx_filename[i] = filename[i];
          if (filename[i] == '\0'){
            sgx_filename[i] = '.';
            sgx_filename[i+1] = 's';
            sgx_filename[i+2] = 'g';
            sgx_filename[i+3] = 'x';
            sgx_filename[i+4] = '\0';
            break;
          }
        }
        mode = "w+"; // fake a directory file, does not fake also works with fflush set ferror to 95. for now fake one.
        memcpy(global_fd_stream_array[fd_stream_idx].file_name , sgx_filename, FILE_NAME_SIZE);
        global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(sgx_filename, mode, key); 
        #if defined(SGX_OCALL_DEBUG)
        errno_code = get_errno();
        printf("%s%d\n", "Debug: Errno is: ", errno_code);
        #endif
        set_errno(0);
        #if defined(SGX_OCALL_DEBUG)
        errno_code = get_errno();
        printf("%s%d\n", "Debug: Errno set to: ", errno_code);
        #endif
      }
    }
    
    global_fd_stream_array[fd_stream_idx].isUsed = 1;
    global_fd_stream_array[fd_stream_idx].fd = fd; // sgx protected_file class actually does not need fd, here fd is used as a index for finding file stream. 
    break;
    // }
  }

  return fd;
}

int sgx_close(int fd){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d\n", __func__, fd);
  ocall_print_string(msg);
  #endif

  int fd_stream_idx = 0;
  int ret;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      ret = sgx_fclose(global_fd_stream_array[fd_stream_idx].file_stream);
      global_fd_stream_array[fd_stream_idx].isUsed = 0; // mark stream empty now
      if (ret != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "%s%s%s%d", "Error: when calling ocall_", __func__, " sgx_fclose ret is ", ret);
        ocall_print_error(error_msg);
      }
      break;
    }
  }

  return ret;
}

ssize_t sgx_read(int fd, void *buf, size_t count){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %lu\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif 

  int fd_stream_idx = 0;
  ssize_t read_bytes = 0;

  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      read_bytes = sgx_fread(buf, 1, count, global_fd_stream_array[fd_stream_idx].file_stream);
      printf("%s: ", "buf from sgx_fread");
      print_buf_hex(buf, read_bytes);
      printf("\n");

      if (read_bytes != count) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Warning: ocall_%s sgx_read failed to read %s; count %ld; read %ld\n", __func__, global_fd_stream_array[fd_stream_idx].file_name, count, read_bytes);
        ocall_print_string(msg);
      }
      break;
    }
  }

  return read_bytes;
}

off_t sgx_lseek(int fd, off_t offset, int whence) {
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; offset %ld; whence %d\n", __func__, fd, offset, whence);
  ocall_print_string(msg);
  #endif

  int fd_stream_idx = 0;
  int ret;
  int err;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      ret = sgx_fseek(global_fd_stream_array[fd_stream_idx].file_stream, offset, whence);
      if (ret != 0) {
        int sgx_errno = sgx_ferror(global_fd_stream_array[fd_stream_idx].file_stream);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "%s%s%s%d%s%d", "Error: when calling ocall_", __func__, " sgx_fseek ret is ", ret, " sgx_ferrno is ", sgx_errno);
        ocall_print_error(error_msg);
      }
      break;
    }
  }

  return offset; // pretend to succeed everytime
} // sgx_fseek

ssize_t sgx_write(int fd, const void *buf, size_t count){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %ld\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif

  int fd_stream_idx = 0;
  ssize_t written_bytes = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      written_bytes = sgx_fwrite(buf, 1, count, global_fd_stream_array[fd_stream_idx].file_stream);
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Debug: ocall_%s writing to file %s; count %ld; written %ld\n", __func__, global_fd_stream_array[fd_stream_idx].file_name, count, written_bytes);
      ocall_print_string(msg);
      #endif
      break;
    }
  }

  return written_bytes;
}

int sgx_fsync(int fd) {
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; \n", __func__, fd);
  ocall_print_string(msg);
  #endif
  
  int fd_stream_idx = 0;
  int ret = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Debug: Flushing ocall_%s with filename %s; \n", __func__, global_fd_stream_array[fd_stream_idx].file_name);
      ocall_print_string(msg);
      #endif
      ret = sgx_fflush(global_fd_stream_array[fd_stream_idx].file_stream);
      if (ret != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "%s%s%s%d", "Error: when calling ocall_", __func__, " sgx_write ret is ", ret);
        ocall_print_error(error_msg);
      }
      break;
    }
  }

  #if defined(SGX_OCALL_DEBUG)
  int errno_code = get_errno();
  printf("%s%d\n", "Debug: Errno is: ", errno_code);
  #endif
  return ret;

} // sgx_fflush

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
  return 0;
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
    return NULL;
  }
  if (ret == NULL) {
    set_errno(err);
  }
  return ret;
}

// u_lstat_ocall or u_lstat64_ocall. disabled by HAVE_LSTAT=0
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


  #if defined(SGX_OCALL_DEBUG)
  snprintf(msg, sizeof(msg), "Debug: err is %d and ret is %d\n", err, ret);
  ocall_print_string(msg);
  #endif

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

  #if defined(SGX_OCALL_DEBUG)
  snprintf(msg, sizeof(msg), "Debug: err is %d and ret is %d\n", err, ret);
  ocall_print_string(msg);
  #endif

  return ret;
}

// u_fstat_ocall or u_fstat64_ocall. 
int sgx_fstat(int fd, struct stat *buf){ // used in 5 locations, seems fine to leave alone if the fd is left open.
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d\n", __func__, fd);
  ocall_print_string(msg);
  #endif

  int ret;
  int err;

  int fd_stream_idx = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      
      sgx_status_t status = u_stat_ocall(&ret, &err, global_fd_stream_array[fd_stream_idx].file_name, buf);
      #if defined(SGX_OCALL_DEBUG)
      printf("%s%s\n", "Debug: filename for u_stat_ocall is: ", global_fd_stream_array[fd_stream_idx].file_name);
      #endif
      if (status != SGX_SUCCESS) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
        ocall_print_error(error_msg);
        set_errno(ESGX);
        return -1;
      }      
      break;
    }
  }
  
  set_errno(err);

  #if defined(SGX_OCALL_DEBUG)
  snprintf(msg, sizeof(msg), "Debug: err is %d and ret is %d\n", err, ret);
  ocall_print_string(msg);
  #endif

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
  int errno_code = get_errno();
  printf("%s%d\n", "Debug: Errno is: ", errno_code);
  #endif
  
  // fcntl is only used for locking .db file. cmd == 6 for SETLK. we return 0 to notice that record lock has been set
  // but it is actually not. we assume no other process will read this .db file.
  return 0;
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
