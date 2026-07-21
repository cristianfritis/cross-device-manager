# Plain main() — no gtest. Drives OUR stack (FwupdUpdateProvider /
# DkmsStatusProvider) end-to-end against a real fwupd daemon; test/vm/
# phase6-smoke.sh invokes the built binary directly (not via ctest).
add_executable(devmgr_fwupd_smoke fwupd_smoke_main.cpp)
target_link_libraries(devmgr_fwupd_smoke PRIVATE devmgr_core devmgr_pal_linux)
target_compile_features(devmgr_fwupd_smoke PRIVATE cxx_std_20)

# Shipped as /usr/bin/devmgr-fwupd-smoke (acceptance-suite spec, task 6.1). The
# release acceptance suite must check firmware THROUGH our FwupdUpdateProvider on
# the *installed* package, but the daemon exposes no firmware verb and the CLI is
# snapshot-only — this is the only headless firmware surface. Same `runtime`
# component as the four main binaries, so it ships in the deb, rpm, and tarball
# alike. Hyphenated OUTPUT_NAME matches the devmgr-tui/devmgr-gui family.
set_target_properties(devmgr_fwupd_smoke PROPERTIES OUTPUT_NAME devmgr-fwupd-smoke)
install(TARGETS devmgr_fwupd_smoke RUNTIME DESTINATION bin COMPONENT runtime)
