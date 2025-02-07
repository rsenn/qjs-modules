#if defined(WIN32) || defined(_MSC_VER) || defined(__MINGW32__)
#include <stdio.h>
#include <windows.h>
#include <winioctl.h>
#include "debug.h"
#include "ioctlcmd.h"

#ifndef FILE_ATTRIBUTE_REPARSE_POINT
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#endif
#ifndef FILE_FLAG_OPEN_REPARSE_POINT
#define FILE_FLAG_OPEN_REPARSE_POINT 0x200000
#endif

#ifndef Newx
#define Newx(v, n, t) v = (t*)malloc((n));
#endif

static bool
get_reparse_data(const char* LinkPath, REPARSE_DATA_BUFFER* rdb) {
  HANDLE hFile;
  DWORD returnedLength;

  int attr = GetFileAttributes(LinkPath);

  if(!(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    return false;
  }

  hFile = CreateFile(LinkPath, 0, 0, 0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, 0);

  if(hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  /* Get the link */
  if(!DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, 0, 0, &rdb->u, 1024, &returnedLength, 0)) {

    CloseHandle(hFile);
    return false;
  }

  CloseHandle(hFile);

  if(rdb->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT && rdb->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
    return false;
  }

  return true;
}

ssize_t
readlink(const char* LinkPath, char* buf, size_t maxlen) {
  REPARSE_DATA_BUFFER rdb;
  wchar_t* wbuf = 0;
  unsigned int u8len, /*len,*/ wlen;

  if(!get_reparse_data(LinkPath, &rdb)) {
    return -1;
  }

  switch(rdb.ReparseTag) {
    case IO_REPARSE_TAG_MOUNT_POINT: { /* Junction */
      wbuf = rdb.u.MountPointReparseBuffer.PathBuffer + rdb.u.MountPointReparseBuffer.SubstituteNameOffset / sizeof(wchar_t);
      wlen = rdb.u.MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
      break;
    }

    case IO_REPARSE_TAG_SYMLINK: { /* Symlink */
      wbuf = rdb.u.SymbolicLinkReparseBuffer.PathBuffer + rdb.u.SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
      wlen = rdb.u.SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
      break;
    }
  }

  if(!wbuf)
    return 0;

  u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, NULL);
  if(u8len >= maxlen)
    u8len = maxlen - 1;
  WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, buf, u8len, NULL, NULL);

  buf[u8len] = '\0';
  return u8len;
}

static DWORD
reparse_tag(const char* LinkPath) {
  REPARSE_DATA_BUFFER rdb;

  if(!get_reparse_data(LinkPath, &rdb)) {
    return 0;
  }

  return rdb.ReparseTag;
}

int
is_symlink(const char* LinkPath) {
  return reparse_tag(LinkPath) == IO_REPARSE_TAG_SYMLINK;
}

char
is_junction(const char* LinkPath) {
  return reparse_tag(LinkPath) == IO_REPARSE_TAG_MOUNT_POINT;
}
#endif
