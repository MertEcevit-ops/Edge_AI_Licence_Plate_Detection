/**
 ******************************************************************************
 * @file    syscall.c
 * @author  MCD/AIS Team
 * @brief   Minimum syscall redefinition
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2019,2021 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/*
 *  minimum syscall redefinition to force Keil AC6 to use functions without breakpoints
 */

#if defined (__CC_ARM) || defined(__ARMCC_VERSION)
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <rt_sys.h>
#define FH_STDIN    0x8001
#define FH_STDOUT   0x8002
#define FH_STDERR   0x8003
/* Standard IO device name defines. */
const char __stdin_name[]  = ":STDIN";
const char __stdout_name[] = ":STDOUT";
const char __stderr_name[] = ":STDERR";
__attribute__((weak))
FILEHANDLE _sys_open (const char *name, int openmode) {
  if (name[0] == ':') {
    if (strcmp(name, ":STDIN") == 0) {
      return (FH_STDIN);
    }
    if (strcmp(name, ":STDOUT") == 0) {
      return (FH_STDOUT);
    }
    if (strcmp(name, ":STDERR") == 0) {
      return (FH_STDERR);
    }
  }
  return (-1);
}
 
 
__attribute__((weak))
int _sys_close (FILEHANDLE fh) {
  switch (fh) {
    case FH_STDIN:
      return (0);
    case FH_STDOUT:
      return (0);
    case FH_STDERR:
      return (0);
  }
  return (-1);
}
__attribute__((weak))
int _sys_write (FILEHANDLE fh, const uint8_t *buf, uint32_t len, int mode) {
  switch (fh) {
    case FH_STDIN:
      return (-1);
    case FH_STDOUT:
      return (0);
    case FH_STDERR:
      return (0);
  }
 
  return (-1);
}
__attribute__((weak))
int _sys_read (FILEHANDLE fh, uint8_t *buf, uint32_t len, int mode) {
  return (-1);
}
__attribute__((weak))
int _sys_istty (FILEHANDLE fh) {
  switch (fh) {
    case FH_STDIN:
      return (1);
    case FH_STDOUT:
      return (1);
    case FH_STDERR:
      return (1);
  }
 
  return (0);
}
 
__attribute__((weak))
int _sys_seek (FILEHANDLE fh, long pos) {
      return (-1);
}

__attribute__((weak))
long _sys_flen (FILEHANDLE fh) {
      return (0);
}
#endif

#if defined(__GNUC__) && !defined(__ARMCC_VERSION)
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/times.h>

extern int __io_putchar(int ch) __attribute__((weak));
extern int __io_getchar(void) __attribute__((weak));

char *__env[1] = { 0 };
char **environ = __env;

void initialise_monitor_handles(void)
{
}

int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit(int status)
{
  _kill(status, -1);
  while (1) {
  }
}

__attribute__((weak))
int _read(int file, char *ptr, int len)
{
  (void)file;

  if (__io_getchar == 0) {
    errno = EBADF;
    return -1;
  }

  for (int data_idx = 0; data_idx < len; data_idx++) {
    *ptr++ = (char)__io_getchar();
  }

  return len;
}

__attribute__((weak))
int _write(int file, char *ptr, int len)
{
  (void)file;

  if (__io_putchar == 0) {
    return len;
  }

  for (int data_idx = 0; data_idx < len; data_idx++) {
    __io_putchar(*ptr++);
  }

  return len;
}

int _close(int file)
{
  (void)file;
  return -1;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _open(char *path, int flags, ...)
{
  (void)path;
  (void)flags;
  errno = ENOENT;
  return -1;
}

int _wait(int *status)
{
  (void)status;
  errno = ECHILD;
  return -1;
}

int _unlink(char *name)
{
  (void)name;
  errno = ENOENT;
  return -1;
}

int _times(struct tms *buf)
{
  (void)buf;
  return -1;
}

int _stat(char *file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _link(char *old, char *new)
{
  (void)old;
  (void)new;
  errno = EMLINK;
  return -1;
}

int _fork(void)
{
  errno = EAGAIN;
  return -1;
}

int _execve(char *name, char **argv, char **env)
{
  (void)name;
  (void)argv;
  (void)env;
  errno = ENOMEM;
  return -1;
}
#endif
