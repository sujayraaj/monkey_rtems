set(ARCH arm )
set(BSP xilinx_zynq_a9_qemu )
set(BSP_CFLAGS "-march=armv7-a -mthumb -mfpu=neon -mfloat-abi=hard -mtune=cortex-a9" )
set(RTEMS_CFLAGS "${BSP_CFLAGS} -O0 -g -qrtems -B${BSP_DIR}/${ARCH}-rtems4.11/lib" )
set(RTEMS_CFLAGS "${RTEMS_CFLAGS} -B${BSP_DIR}/${ARCH}-rtems4.11/xilinx_zynq_a9_qemu/lib/" )
set(RTEMS_CFLAGS "${RTEMS_CFLAGS} --specs bsp_specs")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${RTEMS_CFLAGS}")

find_package(RTEMSNetConf REQUIRED)

# CMake includes
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckIncludeFile)
include(ExternalProject)
include(GNUInstallDirs)

# Set default compiler options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__FILENAME__='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")


# Monkey Version
set(MK_VERSION_MAJOR  1)
set(MK_VERSION_MINOR  6)
set(MK_VERSION_PATCH  0)
set(MK_VERSION_STR "${MK_VERSION_MAJOR}.${MK_VERSION_MINOR}.${MK_VERSION_PATCH}")


# ============================================
# ============= BUILD OPTIONS ================
# ============================================

# Project
option(BUILD_LOCAL         "Build locally, no install"    No)

# Monkey Core
option(WITH_DEBUG          "Build with debug symbols"     No)
option(WITH_ACCEPT         "Use accept(2) system call"   Yes)
option(WITH_ACCEPT4        "Use accept4(2) system call"   No)
option(WITH_LINUX_KQUEUE   "Use Linux kqueue emulator"    No)
option(WITH_TRACE          "Enable Trace mode"            No)
option(WITH_UCLIB          "Enable uClib libc support"    No)
option(WITH_MUSL           "Enable Musl libc support"     No)
option(WITH_BACKTRACE      "Enable Backtrace feature"     No)
option(WITH_LINUX_TRACE    "Enable Lttng support"         No)
option(WITH_PTHREAD_TLS    "Use old Pthread TLS mode"     No)
option(WITH_SYSTEM_MALLOC "Use system memory allocator"  Yes)

# Plugins: what should be build ?, these options
# will be processed later on the plugins/CMakeLists.txt file
option(WITH_PLUGIN_AUTH          "Basic authentication"     No)
option(WITH_PLUGIN_CGI           "CGI support"              No)
option(WITH_PLUGIN_CHEETAH       "Cheetah Shell Interface"  No)
option(WITH_PLUGIN_DIRLISTING    "Directory Listing"        No)
option(WITH_PLUGIN_FASTCGI       "FastCGI"                  No)
option(WITH_PLUGIN_LIANA         "Basic network layer"     Yes)
option(WITH_PLUGIN_LOGGER        "Log Writer"               No)
option(WITH_PLUGIN_MANDRIL       "Security"                 No)
option(WITH_PLUGIN_TLS           "TLS/SSL support"          No)

# Options to build Monkey with/without binary and
# static/dynamic library modes (default is always just
# one target binary).
option(WITHOUT_BIN               "Do not build binary"      No)
option(WITHOUT_CONF              "Skip configuration files" No)
option(WITH_STATIC_LIB_MODE      "Static library mode"     Yes)

# SYSCALL
add_definitions(-DHAVE_SYS_SYSCALL=0)
# This variable allows to define a list of plugins that must
# be included when building the project. The value are the plugins
# names separated by a colon, e.g: -DWITH_PLUGINS=cgi,mbedtls
if(WITH_PLUGINS)
  string(REPLACE "," ";" plugins ${WITH_PLUGINS})
  foreach(name ${plugins})
    message(STATUS "Plugin force enable: ${name}")
    string(TOUPPER ${name} NAME)
    set(WITH_PLUGIN_${NAME} 1)
  endforeach()
endif()

if(WITHOUT_PLUGINS)
  string(REPLACE "," ";" plugins ${WITHOUT_PLUGINS})
  foreach(name ${plugins})
    message(STATUS "Plugin force disable: ${name}")
    string(TOUPPER ${name} NAME)
    set(WITH_PLUGIN_${NAME} 0)
  endforeach()
endif()

if(STATIC_PLUGINS)
  set(STATIC_PLUGINS "${STATIC_PLUGINS},liana")
else()
  set(STATIC_PLUGINS "liana")
endif()

# Variable to be populated by plugins/CMakeLists.txt. It will contain the
# code required to initialize any static plugin.
set(STATIC_PLUGINS_INIT "")
set(STATIC_PLUGINS_LIBS "")

# ===========================================
# ============== DEPENDENCIES ===============
# ===========================================

# Find pthreads
find_package(Threads)

if(WITH_DEBUG)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# Check for accept(2) v/s accept(4)
set(WITH_ACCEPT4 No)
add_definitions(-DACCEPT_GENERIC)

# Check for Linux Kqueue library emulator
if(WITH_LINUX_KQUEUE)
  find_package(Libkqueue REQUIRED)
  if(NOT LIBKQUEUE_FOUND)
    message(FATAL_ERROR "Linux libkqueue was not found." )
  else()
    add_definitions(-DLINUX_KQUEUE)
  endif()
endif()

# Check Trace
if(WITH_TRACE)
  add_definitions(-DTRACE)
endif()

# Check Uclib library
if(WITH_UCLIB)
  add_definitions(-DUCLIB_MODE)
endif()

# Check Musl library
if(WITH_MUSL)
  add_definitions(-DMUSL_MODE)
endif()

# Check Backtrace support
if(WITH_BACKTRACE)
  check_include_file("execinfo.h" HAVE_BACKTRACE)
  if (NOT HAVE_BACKTRACE)
    set(WITH_BACKTRACE No)
  endif()
else()
  add_definitions(-DNO_BACKTRACE)
endif()

# Check for LTTng-UST
if(WITH_LINUX_TRACE)
  check_include_file("lttng/tracepoint.h" HAVE_LTTNG)
  if (NOT HAVE_LTTNG)
    message(FATAL_ERROR "LTTng-UST is not installed in your system." )
  else()
    add_definitions(-DLINUX_TRACE)
  endif()
endif()

# Use old Pthread TLS
if(WITH_PTHREAD_TLS)
  add_definitions(-DPTHREAD_TLS)
endif()

# Use system memory allocator instead of Jemalloc
add_definitions(-DMALLOC_LIBC)

# ============================================
# =========== CONFIGURATION FILES=============
# ============================================

# Default values for conf/monkey.conf
set(MK_CONF_LISTEN       "${NET_CFG_SELF_IP}:${NET_CFG_PORT}")
set(MK_CONF_WORKERS      "8")
set(MK_CONF_TIMEOUT      "15")
set(MK_CONF_PIDFILE      "monkey.pid")
set(MK_CONF_USERDIR      "public_html")
set(MK_CONF_INDEXFILE    "index.html index.htm index.php")
set(MK_CONF_HIDEVERSION  "Off")
set(MK_CONF_RESUME       "On")
set(MK_CONF_USER         "www-data")
set(MK_CONF_KA           "On")
set(MK_CONF_KA_TIMEOUT   "5")
set(MK_CONF_KA_MAXREQ    "1000")
set(MK_CONF_REQ_SIZE     "32")
set(MK_CONF_SYMLINK      "Off")
set(MK_CONF_TRANSPORT    "liana")
set(MK_CONF_DEFAULT_MIME "text/plain")
set(MK_CONF_FDT          "On")
set(MK_CONF_OVERCAPACITY "Resist")

# Default values for conf/sites/default
set(MK_VH_SERVERNAME     "${NET_CFG_SELF_IP}")
set(MK_VH_DOCUMENT_ROOT  MK_DATADIR)
set(MK_VH_LOG_ACCESS     MK_LOGDIR)
set(MK_VH_LOG_ERROR      MK_LOGDIR)

if(BUILD_LOCAL)
  # This mode aims to be backward compatible with older versions of Monkey where
  # a './configure && make' were enough to have the server running without installing
  # any component.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/monkey")
  set(MK_PATH_CONF     "${CMAKE_CURRENT_BINARY_DIR}/conf/")
  set(MK_PATH_PIDFILE  "${CMAKE_CURRENT_BINARY_DIR}")
  set(MK_PATH_WWW      "${CMAKE_CURRENT_SOURCE_DIR}/htdocs/")
  set(MK_PATH_LOG      "${CMAKE_CURRENT_BINARY_DIR}/log/")
  file(MAKE_DIRECTORY  ${MK_PATH_LOG})
else()
  # Custom SYSCONFDIR
  if(NOT INSTALL_SYSCONFDIR)
    set(MK_PATH_CONF ${FILESYSTEM_DIR}/etc/monkey CACHE STRING "Server configuration")
  else()
    set(MK_PATH_CONF ${FILESYSTEM_DIR}/etc/monkey CACHE STRING "Server configuration")
  endif()

  # Custom LOGDIR
  if(NOT INSTALL_LOGDIR)
    set(MK_PATH_LOG ${FILESYSTEM_DIR}/var/log/monkey CACHE STRING "Server logs")
  else()
    set(MK_PATH_LOG ${FILESYSTEM_DIR}/var/log/monkey CACHE STRING "Server logs")
  endif()

  # Custom PIDFILE
  if(NOT PID_FILE)
    set(MK_PATH_PIDFILE ${FILESYSTEM_DIR}/var/run CACHE STRING "Server PID")
  else()
    set(MK_PATH_PIDFILE ${FILESYSTEM_DIR}/var/run CACHE STRING "Server PID")
  endif()

  # Custom WEBROOT
  if(NOT INSTALL_WEBROOTDIR)
    set(MK_PATH_WWW ${FILESYSTEM_DIR}/var/www/monkey CACHE STRING "Server Web documents")
  else()
    set(MK_PATH_WWW ${FILESYSTEM_DIR}/var/www/monkey CACHE STRING "Server Web documents")
  endif()

  # Headers
  if(NOT INSTALL_INCLUDEDIR)
    set(MK_PATH_HEADERS ${CMAKE_INSTALL_INCLUDEDIR}/monkey CACHE STRING "Server header files (development)")
  else()
    set(MK_PATH_HEADERS ${INSTALL_INCLUDEDIR} CACHE STRING "Server header files (development)")
  endif()
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/include/monkey/mk_info.h.in"
  "${PROJECT_SOURCE_DIR}/include/monkey/mk_info.h"
  )

configure_file(
  "${PROJECT_SOURCE_DIR}/include/monkey/mk_env.h.in"
  "${PROJECT_SOURCE_DIR}/include/monkey/mk_env.h"
  )


# General Headers
include_directories(./)
include_directories(include)
include_directories(monkey)

# Instruct CMake to build the the code base
# =========================================
# mk_core  : generic utilities
# plugins  : plugins for mk_server
# mk_server: server code base: plugins, protocols, scheduler.. (no executable)
# mk_bin   : server executable
#
add_subdirectory(mk_core)
add_subdirectory(plugins)
add_subdirectory(mk_server)
if(NOT WITHOUT_BIN)
  add_subdirectory(mk_bin)
endif()
#add_subdirectory(mk_bin/rtems)

# Configuration, headers generation and others
if(NOT WITHOUT_CONF)
  add_subdirectory(conf)
endif()
add_subdirectory(htdocs)
add_subdirectory(include)

# Install (missings ?) paths
install(DIRECTORY DESTINATION ${MK_PATH_LOG})
install(DIRECTORY DESTINATION ${MK_PATH_PIDFILE})
install(DIRECTORY DESTINATION ${MK_PATH_WWW})

