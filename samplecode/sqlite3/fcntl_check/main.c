#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

void main(void) {
    int fd;
    struct flock fl_info;
    int ret;

    fd = open("../bin/test.db", O_RDWR | O_CREAT, 0600);
    if (fd == -1) {
        perror("open()");
        return;
    }

    memset(&fl_info, 0, sizeof(fl_info));

    /* try to get a WRITE lock on the entire file (l_len == zero) */
    fl_info.l_type = F_WRLCK;
    fl_info.l_whence = SEEK_SET;
    fl_info.l_start = 0;
    fl_info.l_len = 0;

    ret = fcntl(fd, F_GETLK, &fl_info);
    if (ret == -1) {
        perror("fcntl(F_GETLK)");
        return;
    }

    if (fl_info.l_type != F_UNLCK) {
        printf("Too bad... it's already locked... by pid=%d\n", fl_info.l_pid);
        return;
    }

    /* try to apply a write lock */
    fl_info.l_type = F_WRLCK;
    ret = fcntl(fd, F_SETLK, &fl_info);
    if (ret == -1) {
        perror("fcntl(F_SETLK)");
        return;
    }

    printf("Success!\n");

    sleep(1);

    printf("I'm done, bye!\n");

    return;
}

