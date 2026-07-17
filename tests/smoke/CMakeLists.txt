# Plain main() — no gtest. Drives OUR stack (FwupdUpdateProvider /
# DkmsStatusProvider) end-to-end against a real fwupd daemon; test/vm/
# phase6-smoke.sh invokes the built binary directly (not via ctest).
add_executable(devmgr_fwupd_smoke fwupd_smoke_main.cpp)
target_link_libraries(devmgr_fwupd_smoke PRIVATE devmgr_core devmgr_pal_linux)
target_compile_features(devmgr_fwupd_smoke PRIVATE cxx_std_20)
