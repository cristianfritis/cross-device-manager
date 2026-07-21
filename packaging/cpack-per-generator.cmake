# Included by cpack once per generator (CPACK_PROJECT_CONFIG_FILE): the deb
# gets FHS-path system files; the tarball gets the packaging/ payload for
# install.sh instead, rooted at the archive top (packaging-deb/-tarball specs).
if(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_COMPONENTS_ALL "runtime;system")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
elseif(CPACK_GENERATOR STREQUAL "RPM")
    # Fedora parity with the deb: the same FHS-path components under /usr, never
    # the tarball payload (packaging-rpm spec).
    set(CPACK_COMPONENTS_ALL "runtime;system")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
elseif(CPACK_GENERATOR STREQUAL "TGZ")
    set(CPACK_COMPONENTS_ALL "runtime;tarball")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    # Component archives skip the top-level dir by default; testers expect
    # devmgr-<version>-linux-x86_64/ at the archive root.
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 1)
endif()
