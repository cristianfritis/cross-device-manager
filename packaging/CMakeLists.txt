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
    include(CPack)
endif()
