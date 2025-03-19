/*
Create a version of the copy that avoids a race condition where a component of the target path 
is changed during the open using open(2) and openat2(2)
*/


#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <libgen.h>
#include <limits.h>


#define BUFSIZE 1024

int openat2_syscall(int dirfd, const char *pathname, struct open_how *how) {
  return syscall(SYS_openat2, dirfd, pathname, how, sizeof(*how));
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "USAGE: ./mvplus source_file target_file\n");
    exit(3); //EXITS IF THERE ARE NOT 3 ARGUMENTS
  }

  char *path_src = argv[1];
  char *path_dest = argv[2];

  //CREATE COPY OF FILE PATH SINCE DIRNAME MODIFIES IT
  char path_src_copy[PATH_MAX];
  char path_dest_copy[PATH_MAX];

  strncpy(path_src_copy, argv[1], PATH_MAX - 1);
  strncpy(path_dest_copy, argv[2], PATH_MAX - 1);

  path_src_copy[PATH_MAX - 1] = '\0';
  path_dest_copy[PATH_MAX - 1] = '\0';

  struct stat src_statbuf, dest_statbuf;
  if (stat(argv[1], &src_statbuf) == 0) { //SOURCE FILE EXISTS

     if ((src_statbuf.st_mode & S_IFMT) != S_IFREG){
           exit(5); //EXITS IF SOURCE IS NOT A REGULAR FILE
     }

      if((src_statbuf.st_mode & S_IFMT) == S_IFDIR){
           exit(5); //EXITS IF SOURCE FILE IS A DIRECTORY
     }

  } else {
      perror("stat");
      exit(4); //EXITS IF SOURCE FILE DOES NOT EXIST
  }

  if (stat(path_dest, &dest_statbuf) == 0) {
    if (src_statbuf.st_ino == dest_statbuf.st_ino &&
        src_statbuf.st_dev == dest_statbuf.st_dev) {
        fprintf(stderr, "Error: Source and destination are the same file.\n");
        exit(EXIT_FAILURE); //EXITS IF SOURCE AND DESTINATION ARE THE SAME FILE
    }
  }

  //GET DIRECTORIES
  char *dir_src = dirname(path_src_copy);
  char *dir_dest = dirname(path_dest_copy);

  int current_dir_fd = open(".", O_RDONLY); //OPEN CURRENT DIRECTORY
  if (current_dir_fd == -1) {
    perror("open current directory");
    exit(EXIT_FAILURE);
  }

  int dir_src_fd = openat(current_dir_fd, dir_src, O_RDONLY); //OPENING SOURCE DIRECTORY
  if (dir_src_fd == -1) {//FAILED TO OPEN SOURCE DIRECTORY
      perror("open directory");
      close(current_dir_fd);
      exit(EXIT_FAILURE);
  }

  close(current_dir_fd);

  struct open_how how = { //USE STRUCT FOR FLAGS
    .flags = O_RDONLY,
  };

  int fd_src = openat2_syscall(dir_src_fd, basename(path_src_copy), &how); //OPENS SOURCE FILE BASED ON THE DIRECTORY ITS IN
  if (fd_src == -1) {//FAILED TO OPEN SOURCE FILE
      perror("openat2 failed for source file");
      close(dir_src_fd);
      exit(EXIT_FAILURE);
  }

  close(dir_src_fd); //CLOSE DIRECTORY AFTER FILE IS OPEN

  int fd_dest;

  struct stat st = {0};
  if (stat(dir_dest, &st) == -1) {
    perror("Destination directory does not exist");
    exit(EXIT_FAILURE);
  }

  if (stat(path_dest, &dest_statbuf) == 0) { //DESTINATION FILE EXISTS

    if ((dest_statbuf.st_mode & S_IFMT) == S_IFDIR) {
        exit(1); //EXITS IF DESTINATION FILE IS A DIRECTORY
    }
    how.flags = O_WRONLY | O_TRUNC; //SET FLAGS -- FILE EXISTS
  } else {
    how.flags = O_WRONLY | O_CREAT | O_TRUNC; //SET FLAGS -- FILE DNE
    how.mode = 0644;
  }

  // OPEN CURRENT DIRECTORY
  current_dir_fd = open(".", O_RDONLY);
  if (current_dir_fd == -1) {
    perror("open current directory");
    exit(EXIT_FAILURE);
  }

  int dir_dest_fd = openat(current_dir_fd, dir_dest, O_RDONLY); //OPEN DESTINATION DIRECTORY RELATIVE TO CURRENT DIRECOTRY
  if (dir_dest_fd == -1) {//FAILED TO OPEN DESTINATION DIRECTORY
    perror("open destination directory");
    close(current_dir_fd);
    exit(EXIT_FAILURE);
  }

  close(current_dir_fd);


  fd_dest = openat2_syscall(dir_dest_fd, basename(path_dest), &how); //OPENS DESTINATION FILE BASED ON THE DIRECTORY ITS IN
  if (fd_dest == -1) {//FAILED TO OPEN DESTINATION FILE
      perror("openat2");
      close(dir_dest_fd);
      exit(EXIT_FAILURE);
  }

  close(dir_dest_fd); //CLOSE DIRECTORY AFTER FILE IS OPEN

  char buffer[BUFSIZE];

  ssize_t bytes_read, bytes_written;
  while ((bytes_read = read(fd_src, buffer, BUFSIZE)) > 0) {
    ssize_t total_written = 0;
    while (total_written < bytes_read) {
        bytes_written = write(fd_dest, buffer + total_written, bytes_read - total_written);
        if (bytes_written == -1) {
            perror("Error writing to destination file");
            close(fd_src);
            close(fd_dest);
            exit(EXIT_FAILURE);
        }
        total_written += bytes_written;
    }
  }

  if (bytes_read == -1) {
    perror("Error reading from source file");
    close(fd_src);
    close(fd_dest);
    exit(EXIT_FAILURE);
  }

  // Close files
  if (-1 == close(fd_dest)) {
      perror("close");
  }
  if (-1 == close(fd_src)) {
      perror("close");
  }

  return 0;

}
