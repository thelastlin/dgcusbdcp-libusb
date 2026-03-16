cmake_minimum_required(VERSION 3.16)

project(dcpd
  DESCRIPTION "Conexant USB Modem Digital Call Progress Daemon"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# dcpd executable
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

add_executable(dcpd
  dcpd.c
  print.c
)

target_link_libraries(dcpd PRIVATE PkgConfig::LIBUSB)

# Installation rules
include(GNUInstallDirs)
install(TARGETS dcpd
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
