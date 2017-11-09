cmake_minimum_required(VERSION 2.8.12)
project (azure_iothub)

# AZURE_IOT_PROTOCOL がセットされていなければ HTTP を用いる

if (NOT DEFINED AZURE_IOT_PROTOCOL)
  message(STATUS "Set \"http\" as AZURE_IOT_PROTOCOL")
  set(AZURE_IOT_PROTOCOL http)
endif ()

#### c++11
if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "-std=gnu++11 ${CMAKE_CXX_FLAGS}")
  endif ()
else ()
  set(CMAKE_CXX_STANDARD 11)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif ()

set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -gstabs ${CMAKE_CXX_FLAGS_DEBUG}")

set(DEPEND_LIB_ROOT ${PROJECT_BINARY_DIR}/dep)
link_directories(${DEPEND_LIB_ROOT}/lib)
link_directories(${DEPEND_LIB_ROOT}/lib64)

add_library(${PROJECT_NAME} STATIC iothub.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${DEPEND_LIB_ROOT}/include
  )

include(ExternalProject)

ExternalProject_Add(azure-iot-sdk-c
  # サブモジュールの取得に失敗することがある
  GIT_REPOSITORY https://github.com/Azure/azure-iot-sdk-c
  GIT_TAG lts_07_2017
  INSTALL_DIR ${DEPEND_LIB_ROOT}
  SOURCE_DIR ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${DEPEND_LIB_ROOT}
  )
# cert.c コンパイル用
target_include_directories(${PROJECT_NAME} PRIVATE ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c/certs)

add_dependencies(${PROJECT_NAME} azure-iot-sdk-c)

if (${AZURE_IOT_PROTOCOL} STREQUAL amqp)
  target_link_libraries(${PROJECT_NAME} INTERFACE
    iothub_client_amqp_transport
    iothub_client_amqp_ws_transport
    uamqp)
  add_definitions(-DAZURE_IOT_AMQP)
elseif (${AZURE_IOT_PROTOCOL} STREQUAL mqtt)
  target_link_libraries(${PROJECT_NAME} INTERFACE
    iothub_client_mqtt_transport
    iothub_client_mqtt_ws_transport
    umqtt)
  add_definitions(-DAZURE_IOT_MQTT)
elseif (${AZURE_IOT_PROTOCOL} STREQUAL http)
  target_link_libraries(${PROJECT_NAME} INTERFACE iothub_client_http_transport)
elseif (DEFINED ${AZURE_IOT_PROTOCOL}) # いずれでもないものが含まれている
  message(FATAL_ERROR "invalid protocol")
endif()

target_link_libraries(${PROJECT_NAME} INTERFACE
  iothub_client
  aziotsharedutil
  ssl
  crypto
  curl
  uuid
  )

## test
set(TEST_NAME azure_iothub_test)
add_executable(${TEST_NAME} iothub_test.cpp)
# add_test(
#   NAME ${TEST_NAME}
#   COMMAND $<TARGET_FILE:${TEST_NAME}>
#   CONFIGURATIONS Debug
#   )

ExternalProject_Add(argagg
  GIT_REPOSITORY https://github.com/vietjtnguyen/argagg
  GIT_TAG 0.4.6
  INSTALL_DIR ${DEPEND_LIB_ROOT}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${DEPEND_LIB_ROOT}
  )

target_include_directories(${TEST_NAME} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_SOURCE_DIR}/common
  ${DEPEND_LIB_ROOT}/include
  )

add_dependencies(${TEST_NAME} argagg)
target_link_libraries(${TEST_NAME} ${PROJECT_NAME} pthread)
