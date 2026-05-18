# Overlay triplet shadowing the built-in x86-windows-static.
# See triplets/x64-windows-static.cmake for the rationale; the
# toolset pin must match dev-env.ps1 and CI's msvc-dev-cmd.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET_VERSION "14.44")
