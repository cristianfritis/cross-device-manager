# Tarball-only payload (packaging-tarball spec): everything install.sh needs,
# gathered under packaging/ inside the archive. The deb never includes this
# component — it uses the FHS-path "system" component from daemon/ instead.
install(PROGRAMS devmgrd.initd install.sh uninstall.sh
        DESTINATION packaging COMPONENT tarball)
install(FILES manifest.sh DESTINATION packaging COMPONENT tarball)
install(FILES ${CMAKE_SOURCE_DIR}/daemon/data/devmgrd.service
              ${CMAKE_SOURCE_DIR}/daemon/data/org.devmgr.Manager1.service
              ${CMAKE_SOURCE_DIR}/daemon/data/org.devmgr.Manager1.conf
              ${CMAKE_SOURCE_DIR}/daemon/data/org.devmgr.policy
        DESTINATION packaging COMPONENT tarball)

# License, in both artifacts. Debian policy requires /usr/share/doc/<pkg>/copyright
# (the deb's prefix is /usr, so the destination is relative); the tarball carries it
# at the archive root next to the README-facing install.sh.
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION share/doc/devmgr RENAME copyright COMPONENT system)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION . COMPONENT tarball)

if(DEVMGR_PACKAGED_BUILD)
    # CPack: one deb (Ubuntu) + one generic tarball from the same build tree
    # (packaging-deb / packaging-tarball specs). Component selection and the
    # per-generator install prefix live in cpack-per-generator.cmake.
    set(CPACK_PACKAGE_NAME "devmgr")
    set(CPACK_PACKAGE_VENDOR "cross-device-manager")
    set(CPACK_PACKAGE_CONTACT "Cristian Fritis <cristian.fritis.palacios@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Linux device manager: devices, drivers, firmware, and state snapshots")
    set(CPACK_PACKAGE_DESCRIPTION
        "devmgr manages PCI/USB devices, kernel modules, driver binding, firmware\nupdates (via fwupd), and system-state snapshots with rollback. Ships the\ndevmgrd daemon (D-Bus activated), a Qt 6 GUI, an FTXUI TUI, and a recovery CLI.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/cristianfritis/cross-device-manager")
    set(CPACK_PACKAGE_VERSION "${DEVMGR_FULL_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "devmgr-${DEVMGR_FULL_VERSION}-linux-x86_64")
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cpack-per-generator.cmake")

    # Debian versioning: '~' orders prereleases before the final release
    # (0.5.0~beta.1 < 0.5.0), unlike semver's '-'.
    if(DEVMGR_PRERELEASE)
        set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~${DEVMGR_PRERELEASE}")
    else()
        set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
    endif()
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
    # Per-series runtime Depends (Qt6, libudev, libkmod, ...) resolved from the
    # actual linked libraries by dpkg-shlibdeps inside the packaging container.
    # qt6-qpa-plugins is a FILE dependency shlibdeps can't see: without it Qt
    # has no offscreen/alternate platform plugins (VM install smoke caught it).
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/deb/prerm;${CMAKE_CURRENT_SOURCE_DIR}/deb/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

    # --- RPM (packaging-rpm spec): Fedora parity with the deb — same file set,
    # same static sdbus-c++; only the dependency names and scriptlet dialect
    # differ. RPM is deliberately NOT in the default CPACK_GENERATOR: the
    # release deb/tgz are cut in an Ubuntu container that has no rpmbuild, so
    # the rpm is produced separately with `cpack -G RPM` in the Fedora
    # container (Dockerfile.fedora / the pkg-rpm compose service). These
    # CPACK_RPM_* variables are inert on the deb/tgz runs and apply only there.
    set(CPACK_RPM_COMPONENT_INSTALL ON)
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # rpm forbids '-' in Version (it separates version from release), so
    # prereleases use '~', which rpm — like dpkg — orders before the final.
    if(DEVMGR_PRERELEASE)
        set(CPACK_RPM_PACKAGE_VERSION "${PROJECT_VERSION}~${DEVMGR_PRERELEASE}")
    else()
        set(CPACK_RPM_PACKAGE_VERSION "${PROJECT_VERSION}")
    endif()
    # Fedora package names for the needs autoreq can't map from a soname — the
    # Qt platform plugins (offscreen/xcb ship inside qt6-qtbase-gui, the deb's
    # qt6-qpa-plugins analogue). systemd-libs carries libudev + libsystemd;
    # kmod-libs carries libkmod. autoreq (ON by default) adds the remaining
    # soname deps (libQt6*, libstdc++, ...). sdbus-c++ is static → no dep.
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase-gui, systemd-libs, kmod-libs")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/post")
    set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rpm/preun")
    # Don't claim ownership of directories systemd/dbus/polkit already own. rpm
    # permits shared directory ownership, but well-behaved packages stay out of
    # foreign dirs so `rpm -V` and erase never touch them.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/lib/systemd /usr/lib/systemd/system
        /usr/share/dbus-1 /usr/share/dbus-1/system-services /usr/share/dbus-1/system.d
        /usr/share/polkit-1 /usr/share/polkit-1/actions)

    include(CPack)
endif()
