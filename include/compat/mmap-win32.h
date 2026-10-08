/*
 * Light version of mmap() / munmap() for Windows
 *
 * Copyright (C) 2011 Daniel Diaz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and
 * the GNU Lesser General Public License along with this program.  If
 * not, see http://www.gnu.org/licenses/.
 */

/* Include mmap-win32.h instead of sys/mman.h and link with mmap-sys32.o */

#ifndef _MMAP_WIN32_H
#define _MMAP_WIN32_H

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

typedef void* caddr_t;

unsigned int sleep(unsigned int seconds);

long getpagesize(void);
long getgranularity(void);

#define MAP_FAILED ((void*)-1)

#define PROT_READ 0x1  /* Page can be read.  */
#define PROT_WRITE 0x2 /* Page can be written.  */
#define PROT_EXEC 0x4  /* Page can be executed.  */
#define PROT_NONE 0x0  /* Page can not be accessed.  */

#define MAP_SHARED 0x01    /* Share changes.  */
#define MAP_PRIVATE 0x02   /* Changes are private.  */
#define MAP_TYPE 0x0f      /* Mask for type of mapping.  */
#define MAP_FIXED 0x10     /* Interpret addr exactly.  */
#define MAP_ANONYMOUS 0x20 /* Don't use a file.  */

void* mmap(void* addr, size_t len, int prot, int flags, int fd, long off);
int munmap(void* addr, size_t len);
int msync(void* addr, size_t length, int flags);
int mprotect(void* addr, size_t length, int prot);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* !_MMAP_H */
