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

// For open64 need to define this
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#define ESGX 0xffff /* SGX ERROR*/
#define MAX_FD_STREAM_ARRAY_SIZE 0x14 // #define FOPEN_MAX     20
#define FILE_NAME_SIZE 0xff // #define FILENAME_MAX  260. so 0xff = 256 < 260 is fine

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

struct Fd_stream {
  SGX_FILE* file_stream;
  char file_name[FILE_NAME_SIZE];
  int fd; // zero initialized by-default
  // int sgx_fd;
  int Used; // indicates if this item is set or not, 0 for notUsed (false), 1 for Used (true)
};

typedef struct Fd_stream Fd_stream;

Fd_stream global_fd_stream_array[MAX_FD_STREAM_ARRAY_SIZE];

sgx_key_128bit_t key = {1};
int global_fd = 3;

off_t lseek64(int fd, off_t offset, int whence);
int open64(const char *filename, int flags, ...);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int close(int fd);

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

  #ifdef SGX_FD_SHADOW
  mode_t mode2 = 0; // file permission bitmask, set by passed in mode.
  // Get the mode from arguments
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list valist;
		va_start(valist, flags); // Init a variable argument list
		mode2 = va_arg(valist, mode_t); // Retrieve next value with specified type
		va_end(valist); // End using variable argument list
	}

  int fd = open64(filename, flags, mode2);  // create shadow file
  #if defined(SGX_OCALL_DEBUG)
  snprintf(msg, sizeof(msg), "Debug: open64 create a fd of %d for %s\n", fd, filename);
  ocall_print_string(msg);
  #endif
 
  if (fd == -1){ // failed to create/open file
    return fd;
  }
  #endif
  
  // // fool the sqlite about file its opening now
  // else {
  //   close(fd); // close fd, so won't have issue with open, only depends on sgx_open
  // }
  
  // `”w”` will create an empty file for writing. If the file already exists the existing file will be deleted or erased and the new empty file will be used. Be cautious while using these options.
  // `”w+”` will create an empty file for both reading and writing.

  // length of file name must be shorter than FILE_NAME_SIZE - 5, check later
  // char sgx_file_extension[5] = ".sgx";
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

  // int sgx_fd = open64(sgx_filename, flags, mode2); // open another file just for sgx file system
  // if (sgx_fd == -1){ // failed to create/open file
  //   return sgx_fd; // make sure file exist
  // }

  // close(sgx_fd); // make a shadow file. no need to keep it open

  int fd = global_fd;
  global_fd += 1; // everytime open a new fd, never recycle, maybe impl recycle later

  // exist valid fd 
  const char* mode = "r+"; // read the file
  int fd_stream_idx = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    // check if stream exists
    if (global_fd_stream_array[fd_stream_idx].Used == 1){ 
      continue;
    }
    // check file name. strcmp == 0 for equal. 
    // if( strcmp(global_fd_stream_array[fd_stream_idx].file_name, sgx_filename) == 0){
    //   // shadow fd completed for this stream if !=0.
    //   if (global_fd_stream_array[fd_stream_idx].fd != 0){ 
    //     break; 
    //   }
    //   else {
    //       char error_msg[256];
    //       snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: fopen succeeded but open failed for ", sgx_filename);
    //       ocall_print_error(error_msg);
    //       break;
    //   }
    // } else {
    memcpy(global_fd_stream_array[fd_stream_idx].file_name , sgx_filename, FILE_NAME_SIZE);

    
    // sgx_fopen is compatible with c++ fopen, however, its semantics differs from os syscall open64. it calls open(), close()
    global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(sgx_filename, mode, key); 
    int error_code = sgx_ferror(global_fd_stream_array[fd_stream_idx].file_stream);

    if (global_fd_stream_array[fd_stream_idx].file_stream == NULL) {
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Warning: Entering ocall_%s with filename %s; error code %d; use mode w+;\n", __func__, sgx_filename, error_code);
      ocall_print_string(msg);
      #endif
      mode = "w+"; // w is write-only, can't read.
      global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(sgx_filename, mode, key); 
    }

    if (error_code > 0){ // EISDIR 22, Is a directory, < 0 is -1, is NULL stream
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Warning: Entering ocall_%s with filename %s; error code %d\n", __func__, sgx_filename, error_code);
      ocall_print_string(msg);
      #endif 
    }

    // if (error_code == ENOENT || ){ // No such file or directory, need to use w+
    //   #if defined(SGX_OCALL_DEBUG)
    //   char msg[256];
    //   snprintf(msg, sizeof(msg), "Warning: Entering ocall_%s with filename %s; error code %d\n", __func__, sgx_filename, error_code);
    //   ocall_print_string(msg);
    //   #endif
    //   mode = "w+"; // w is write-only, can't read.
    //   global_fd_stream_array[fd_stream_idx].file_stream = sgx_fopen(sgx_filename, mode, key); 
    // }
    
    global_fd_stream_array[fd_stream_idx].Used = 1;
    global_fd_stream_array[fd_stream_idx].fd = fd; // sgx protected_file class actually does not need fd, here fd is used as a index for finding file stream. 
    // global_fd_stream_array[fd_stream_idx].sgx_fd = sgx_fd; // set the mapping between sgx_fd and fd 
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
      global_fd_stream_array[fd_stream_idx].Used = 0; // mark stream empty now
      if (ret != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "%s%s%s%d", "Error: when calling ocall_", __func__, " sgx_fclose ret is ", ret);
        ocall_print_error(error_msg);
      }
      break;
    }
  }

  #ifdef SGX_FD_SHADOW
  ret = close(fd);
  #endif

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

  // read_bytes = read(fd, buf, count); // shadow read to move file offset
  // printf("%s: ", "buf from linux read");
  // print_buf_hex(buf, read_bytes);
  // printf("\n");

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

  // read_bytes = read(fd, buf, count); // TEST: shadow read to move file offset
  
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
    	// // check for fseek out of boundary
      // sgx_fseek(global_fd_stream_array[fd_stream_idx].file_stream, 0, SEEK_END);
	    // uint64_t file_size = sgx_ftell(global_fd_stream_array[fd_stream_idx].file_stream);
      // sgx_fseek(global_fd_stream_array[fd_stream_idx].file_stream, 0, SEEK_SET);
      // // skip offset cross bound
      // if (offset > file_size){
      //   char error_msg[256];
      //   snprintf(error_msg, sizeof(error_msg), "%s%s%s%ld%s%lu", "Warning: when calling ocall_", __func__, " offset is ", offset, " larger than file size ", file_size);
      //   ocall_print_error(error_msg); 
      //   break;
      // }
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

  #ifdef SGX_FD_SHADOW  
  // lseek64 returns the offset location as measured in bytes from the beginning of the file.
  off_t new_offset = lseek64(fd, offset, whence); // this will return different offset than sgx_fseek has
  #if defined(SGX_OCALL_DEBUG)
  snprintf(msg, sizeof(msg), "Debug: new_offset for shadow file is %ld\n", new_offset);
  ocall_print_string(msg);
  #endif
  
  return new_offset;
  #endif
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
      printf("%s: ", "buf of sgx_fwrite");
      print_buf_hex(buf, written_bytes);
      printf("\n");
      #if defined(SGX_OCALL_DEBUG)
      char msg[256];
      snprintf(msg, sizeof(msg), "Debug: ocall_%s writing to file %s; count %ld; written %ld\n", __func__, global_fd_stream_array[fd_stream_idx].file_name, count, written_bytes);
      ocall_print_string(msg);
      #endif
      break;
    }
  }


  #ifdef SGX_FD_SHADOW
  // 0-nize buffer. create shadow file
  // for (int i=0; i< count; i++){
  // for (int i=0; i< written_bytes; i++){
  //   *((unsigned char*)buf + i) = '1';
  // }

  written_bytes = write(fd, buf, written_bytes);
  #endif

  return written_bytes;
  // return write(fd, buf, count);
  // return write(fd, buf, count); // not encrypted for now
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

  #ifdef SGX_FD_SHADOW
  ret = fsync(fd);
  #endif 

  return ret;

} // sgx_fflush


// replace by sgx_file::u_open64_ocall, maybe sgx_fopen_auto_key
int open64(const char *filename, int flags, ...){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with filename %s; flags %d\n", __func__, filename, flags);
  ocall_print_string(msg);
  #endif

  int fd;
  int err;

  mode_t mode2 = 0; // file permission bitmask, set by passed in mode.
  // Get the mode from arguments
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list valist;
		va_start(valist, flags); // Init a variable argument list
		mode2 = va_arg(valist, mode_t); // Retrieve next value with specified type
		va_end(valist); // End using variable argument list
	}

  // sgx_status_t status = ocall_open64(&ret, filename, flags, mode2);
  sgx_status_t status = u_open64_ocall(&fd, &err, filename, flags, mode2);
  if (status != SGX_SUCCESS) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: SGX fails when calling ocall_", __func__);
    ocall_print_error(error_msg);
    set_errno(ESGX);
    return -1;
  }
  if (fd == -1) {
    set_errno(err); // if open fails, set global errno to err
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "%s%d%s%d", "Error: open failed for errno ", err, " and fd ", fd);
    ocall_print_error(error_msg);
  }

  return fd;
}

// replace by sgx_file::u_lseek_ocall, maybe sgx_fseek
off_t lseek64(int fd, off_t offset, int whence){
  #if defined(SGX_OCALL_DEBUG)
  char msg[256];
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; offset %ld; whence %d\n", __func__, fd, offset, whence);
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
int fsync(int fd){ // fsync(fd) shows up 3 times, change to sgx_fsync, replace fdatasync to sgx_fsync also. (# define fdatasync sgx_fsync)
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


  char sgx_path[FILE_NAME_SIZE];
  for (int i =0; i < FILENAME_MAX - 5; i++) {
    sgx_path[i] = path[i];
    if (path[i] == '\0'){
      sgx_path[i] = '.';
      sgx_path[i+1] = 's';
      sgx_path[i+2] = 'g';
      sgx_path[i+3] = 'x';
      sgx_path[i+4] = '\0';
      break;
    }
  }

  sgx_status_t status = u_lstat_ocall(&ret, &err, sgx_path, buf);
  // sgx_status_t status = u_lstat_ocall(&ret, &err, path, buf);

  #if defined(SGX_OCALL_DEBUG)
  printf("%s%s\n", "DEBUG: sgx_path for u_lstat_ocall is: ", sgx_path);
  #endif

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

  char sgx_path[FILE_NAME_SIZE];
  for (int i =0; i < FILENAME_MAX - 5; i++) {
    sgx_path[i] = path[i];
    if (path[i] == '\0'){
      sgx_path[i] = '.';
      sgx_path[i+1] = 's';
      sgx_path[i+2] = 'g';
      sgx_path[i+3] = 'x';
      sgx_path[i+4] = '\0';
      break;
    }
  }

  sgx_status_t status = u_stat_ocall(&ret, &err, sgx_path, buf);
  #if defined(SGX_OCALL_DEBUG)
  printf("%s%s\n", "DEBUG: sgx_path for u_stat_ocall is: ", sgx_path);
  #endif
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

  // ret = 0;
  // err = 0;
  // buf->st_dev = 73; // The st_dev field describes the device on which this file resides.
  // buf->ino = 42079737; // /* inode number */
  // buf->st_mode = 33188; // This field contains the file type and mode.
  // buf->st_nlink = 1; /* number of hard links */
  // buf->st_uid = 0;
  // buf->st_gid = 0;
  // buf->st_rdev = 0;
  // buf->st_size = 0; 
  // buf->st_uid = 0;
  // buf->st_blksize = 4096; /* blocksize for file system I/O */
  // buf->st_blocks = 0; /* number of 512B blocks allocated */
  // buf->st_atim = 1607635982; /* time of last access */
  // buf->st_mtim = 1607635982; /* time of last modification */
  // buf->st_ctim = 1607635982; /* time of last status change */

  // need stat about sgx_file not the shadow file. e.g. st_size, st_blocks
  // fstat use fd, stat use pathname, use stat to replace this
  // sgx_status_t status = u_fstat_ocall(&ret, &err, fd, buf);

  int fd_stream_idx = 0;
  for (; fd_stream_idx < MAX_FD_STREAM_ARRAY_SIZE; fd_stream_idx++){
    if (global_fd_stream_array[fd_stream_idx].fd == fd){
      
      sgx_status_t status = u_stat_ocall(&ret, &err, global_fd_stream_array[fd_stream_idx].file_name, buf);
      #if defined(SGX_OCALL_DEBUG)
      printf("%s%s\n", "DEBUG: filename for u_stat_ocall is: ", global_fd_stream_array[fd_stream_idx].file_name);
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
  
  // if (ret == -1) {
  set_errno(err);
  // }

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
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %lu\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif 
  long unsigned int ret;
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
  snprintf(msg, sizeof(msg), "Debug: Entering ocall_%s with fd %d; count %lu\n", __func__, fd, count);
  ocall_print_string(msg);
  #endif 
  long unsigned int ret;
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
  // shadow file
  sgx_status_t status = u_unlink_ocall(&ret, &err, pathname);
  if (status != SGX_SUCCESS) {
      char error_msg[256];
      snprintf(error_msg, sizeof(error_msg), "%s%s", "Error: when calling ocall_", __func__);
      ocall_print_error(error_msg);
  }


  // sgx file
  char sgx_pathname[FILE_NAME_SIZE];
  for (int i =0; i < FILENAME_MAX - 5; i++) {
    sgx_pathname[i] = pathname[i];
    if (pathname[i] == '\0'){
      sgx_pathname[i] = '.';
      sgx_pathname[i+1] = 's';
      sgx_pathname[i+2] = 'g';
      sgx_pathname[i+3] = 'x';
      sgx_pathname[i+4] = '\0';
      break;
    }
  }
  status = u_unlink_ocall(&ret, &err, sgx_pathname);
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
