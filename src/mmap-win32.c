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

/* Include mmap-win32.h instead of sys/mman.h and link with mmap-win32.o */

#ifdef _WIN32

#include <errno.h>
#include <windows.h>
#include <io.h>

#include "mmap-win32.h"

unsigned int
sleep(unsigned int seconds) {
  Sleep(seconds * 1000);
  return 0;
}

long
getpagesize(void) {
  static long pagesize = 0;
  if(pagesize == 0) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    pagesize = si.dwPageSize;
  }
  return pagesize;
}

long
getgranularity(void) {
  static long granularity = 0;
  if(granularity == 0) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    granularity = si.dwAllocationGranularity;
  }
  return granularity;
}

#define RoundUp(x, y) (((x) + ((y)-1)) / (y))

/* inspired by cygwin */
void*
mmap(void* addr, size_t len, int prot, int flags, int fd, long off) {
  DWORD pageProtect, access;
  HANDLE hFile, hMap;

  long granularity = getgranularity();

  if(off % getpagesize() || (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE)) ||
     ((flags & MAP_SHARED) && (flags & MAP_PRIVATE)) ||
     ((flags & MAP_FIXED) && ((LONG_PTR)addr % granularity)) || !len) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  access = (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
  if(flags & MAP_PRIVATE)
    access = FILE_MAP_COPY;

  if(access & FILE_MAP_COPY)
    pageProtect = PAGE_WRITECOPY;
  else if(access & FILE_MAP_WRITE)
    pageProtect = PAGE_READWRITE;
  else
    pageProtect = PAGE_READONLY;

  pageProtect |= SEC_RESERVE;

  if(flags & MAP_ANONYMOUS)
    fd = -1;

  /* Map always in multipliers of `granularity'-sized chunks. */
  off = off & ~(granularity - 1);
  len = RoundUp(len, granularity) * granularity;

  hFile = (HANDLE)_get_osfhandle(fd);

  if(fd != -1) { /* we should check if fd is open */
    if(hFile == INVALID_HANDLE_VALUE) {
    err:
      errno = EBADF;
      return MAP_FAILED;
    }

    if(GetFileType(hFile) == FILE_TYPE_DISK) {
      DWORD file_size = GetFileSize(hFile, NULL);

      file_size -= off;
      if(len > file_size)
        len = file_size;
    } else if(GetLastError() != NO_ERROR)
      goto err;

    /* check if /dev/zero ? else set fd = -1 (MAP_ANONYMOUS) and
     * and hFile = INVALID_HANDLE_VALUE */
  }

  hMap = CreateFileMapping(hFile, NULL, pageProtect, 0, hFile == INVALID_HANDLE_VALUE ? len : 0, NULL);
  if(hMap == NULL) {
    errno = EINVAL; /* what else ? */
    return MAP_FAILED;
  }

  void* addr1 = MapViewOfFileEx(hMap, access, 0, off, len, (flags & MAP_FIXED) ? addr : NULL);

  if(!addr1 || ((flags & MAP_FIXED) && addr1 != addr)) {
    errno = EINVAL;
    CloseHandle(hMap);
    return MAP_FAILED;
  }

  /* should save hMap to closeHandle(hMap) at unmap... memory leak */
  errno = 0;
  return addr1;
}

int
munmap(void* addr, size_t len) {
  if(((LONG_PTR)addr % getpagesize())) /* maybe test also || len == 0 */
  {
    errno = EINVAL;
    return -1;
  }

  if(UnmapViewOfFile(addr)) {
#if 0 /* be cool here */
      errno = EINVAL;
      return -1;
#endif
  }

  /* we should CloseHandle(hMap); */
  errno = 0;
  return 0;
}

int
msync(void* addr, size_t length, int flags) {
  if(FlushViewOfFile(addr, length))
    return 0;
  return -1;
}

int
mprotect(void* addr, size_t length, int prot) {
  DWORD newProtect, oldProtect;

  if(prot & PROT_EXEC)
    newProtect = (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
  else
    newProtect = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;

  if(VirtualProtect(addr, length, newProtect, &oldProtect))
    return 0;

  return -1;
}

#endif /* _WIN32 */
