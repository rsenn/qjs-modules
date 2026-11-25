## qjs-* cross builds (MSYS/mingw) — session handoff

**Decisions:**
- MSYS toolchain files (/opt/cmake-toolchains/msys32|64.cmake) use CMAKE_SYSTEM_NAME MSYS — MSYS compilers define __MSYS__/__CYGWIN__ not _WIN32, so libwebsockets picks plat/unix; CMake must agree (UNIX, not WIN32). Patch 0010 in qjs-lws/patches covers net/route.h and ETHER_ADDR_LEN. Verified: libwebsockets target builds for msys32 with -DWITH_SSL=OFF
- cfg-mingw*/cfg-msys* in cfg.sh force -DPKG_CONFIG_EXECUTABLE=$(which <triplet>-pkg-config) and assign PKG_CONFIG unconditionally — the triplet wrapper (c-utils pkgcfg) rewrites foreign .pc prefixes; env PKG_CONFIG must not override it
- Absolute path required for that flag — a bare name was resolved relative to the build dir by CMake ("Could NOT find PkgConfig" in qjs-imgui)
- Committed/pushed: qjs-lws e810526; quickjs 5576422; c-utils fef1866e; plot-cv 2241073; qjs-opencv 5b9a25a (no Co-Authored-By, per CLAUDE.md)

**Open / blocked:**
- Full qjs-lws build for MSYS untested; `if(WIN32)` link libs (crypt32, iphlpapi) skipped under SYSTEM_NAME MSYS
- BuildLibwebsockets.cmake ~384/388: dev warnings, endif() arg mismatch (user WIP)
- MSYS sysroot lacks OpenSSL (msys2-get.sh -s <triplet> openssl); existing msys build trees need reconfigure
- Not committed: /opt/cmake-toolchains edits (separate repo), opencv/cfg.sh (untracked), quickjs.def js_module_normalize_path, qjs-ffi WIP, qjs-modules BUGS/quickjs-path.c/quickjs-sockets.c/TODO.md
- qjs-imgui mingw configure not re-run after the absolute-path fix
- TODO.md item 8 threads remain: static musl, same dep gating for qjs-lws/qjs-ffi, c-utils pkgcfg typo gap

**Conventions noted:**
- Commit only when asked; leave scratch/untracked files out; no force pushes; pull --rebase --autostash when a push is rejected
