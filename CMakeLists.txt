cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(FakeCamera C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -Wextra -Wno-unused-parameter -O3 -std=gnu99")

add_executable(fakecamera
  main.c
)

target_link_libraries(fakecamera
  taihen_stub
  SceLibKernel_stub
  SceKernelThreadMgr_stub
  SceAppMgr_stub
  SceSysmem_stub
  SceIofilemgr_stub
  k
  gcc
)

set_target_properties(fakecamera
  PROPERTIES LINK_FLAGS "-nostdlib"
)

vita_create_self(fakecamera.suprx fakecamera
  UNSAFE
  CONFIG ${CMAKE_SOURCE_DIR}/export.yml
)
