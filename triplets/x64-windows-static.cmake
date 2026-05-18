# Overlay shadowing built-in x64-windows-static: same static
# CRT + libs, plus a toolset pin. vcpkg builds ports with its
# own newest toolset unless pinned here; it must match the
# toolset dev-env.ps1 selects (and CI's msvc-dev-cmd) or the
# vcpkg deps and mdview link mismatched STLs.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET_VERSION "14.44")
