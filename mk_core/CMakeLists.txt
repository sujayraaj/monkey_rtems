SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")

set(src
  mk_iov.c
  mk_file.c
  mk_rconf.c
  mk_string.c
  mk_memory.c
  mk_event.c
  mk_utils.c
  mk_rbtree.c
  )

include_directories(include)
add_library(mk_core STATIC ${src})
target_link_libraries(mk_core ${CMAKE_THREAD_LIBS_INIT})

if(NOT WITH_SYSTEM_MALLOC)
  target_link_libraries(mk_core libjemalloc)
endif()

if(NOT BUILD_LOCAL)
  install(DIRECTORY
      ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION
      ${MK_PATH_HEADERS})
endif()

