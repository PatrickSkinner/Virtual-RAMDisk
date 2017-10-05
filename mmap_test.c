#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <malloc.h>

//#define MMAP_DEV_CMD_GET_BUFSIZE 1  /* defines our IOCTL cmd */
#define MYIOC_TYPE 'k'
#define SET_NPROC_OP 1
#define ASGN1_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, sizeof(int)) 



ssize_t my_fread(int fildes, void *buf, size_t nbyte) {
    ssize_t read_size;
    ssize_t to_read_size = nbyte;

    while (to_read_size > 0)  {
      read_size = read(fildes, buf, to_read_size);

      if (read_size < 0) {
          if (EINTR == errno) {
              continue;
          } else {
              perror("read()");
              exit(EXIT_FAILURE);
          }
      } else if (0 == read_size) {
          /* EOF reached */
          break;
      }

      buf += read_size;
      to_read_size -= read_size;
    }      
    
    return nbyte - to_read_size;
}


ssize_t my_fwrite(int fildes, const void *buf, size_t nbyte) {
    ssize_t to_write_size = nbyte;
    ssize_t written_size;

    while (to_write_size > 0) {
        written_size = write(fildes, buf, to_write_size);
         if (written_size < 0) {
             if (EINTR == errno) {
                 continue;
             } else {
                 perror("write()");
                 exit(EXIT_FAILURE);
             }
         }
         buf += written_size;
         to_write_size -= written_size;
    } 

    return nbyte;
}


void read_and_compare (int fd, char *read_buf, char *mmap_buf,
                       unsigned long len)
{
    /* Read the file and compare with mmap_buf[] */

    if ((unsigned long)my_fread (fd, read_buf, len) != len) {
        fprintf (stderr, "read problem:  %s\n", strerror (errno));
        exit (1);
    }
    if (memcmp (read_buf, mmap_buf, len) != 0) {
        fprintf (stderr, "buffer miscompare\n");
        exit (1);
    }
}

#define SIZE 1024 * 64

int main (int argc, char **argv)
{
    fprintf( stderr, "mmap_test start");
    unsigned long i, j;
    int fd;
    char *buf, *read_buf, *mmap_buf, *filename = "/dev/asgn1";
    int nproc = 12345;

    srandom (getpid ());

    if (argc > 1)
        filename = argv[1];

    if ((fd = open (filename, O_RDWR)) < 0) {
        fprintf (stderr, "open of %s failed:  %s\n", filename,
                 strerror (errno));
        exit (1);
    }

    assert(buf = malloc(SIZE));

    for (i = 0; i < SIZE; i++) {
        buf[i] = random() % 256;
    }

    if (SIZE != my_fwrite(fd, buf, SIZE)) {
        fprintf (stderr, "write problem:  %s\n", strerror (errno));
        exit(1);
    }

#if 0
    /* have the driver tell us the buffer size */
    if (ioctl (fd, MMAP_DEV_CMD_GET_BUFSIZE, &len) < 0) {
        fprintf (stderr, "ioctl failed:  %s\n", strerror (errno));
        exit (1);
    }
    printf ("driver's ioctl says buffer size is %ld\n", len);
#endif

    assert(read_buf = malloc(SIZE));

    mmap_buf = mmap (NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mmap_buf == (char *)MAP_FAILED) {
        fprintf (stderr, "mmap of %s failed:  %s\n", filename,
                 strerror (errno));
        exit (1);
    }
    printf ("mmap succeeded:  %p\n", mmap_buf);

    (void)lseek (fd, 0, SEEK_SET);
    read_and_compare (fd, buf, mmap_buf, SIZE);
    printf ("comparison of same data via read() and mmap() successful\n");


    /* Change one randomly chosen byte in the mmap region */

    j = random () % SIZE;
    *(mmap_buf + j) = random () % j;
    printf ("modified mmap_buf\n");

    /*  repeat the read-back comparison. */
    (void)lseek (fd, 0, SEEK_SET);
    read_and_compare (fd, read_buf, mmap_buf, SIZE);
    printf ("comparison of modified data via read() and mmap() successful\n");

    (void)lseek (fd, 0, SEEK_SET);
//ASGN1_SET_NPROC
    if (ioctl (fd, SET_NPROC_OP, &nproc) < 0) {
        fprintf (stderr, "ioctl hella failed:  %s\n", strerror (errno));
        exit (1);
    }
    printf("nproc set to %d\n", nproc);
    return 0;
}
