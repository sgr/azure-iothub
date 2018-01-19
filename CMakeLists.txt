cmake_minimum_required(VERSION 2.8.12)
project (azure_iothub)
set(VER_MAJOR 1)
set(VER_MINOR 0)
set(VER_REVISION 0)
set(VERSION ${VER_MAJOR}.${VER_MINOR}.${VER_REVISION})

# AZURE_IOT_PROTOCOL がセットされていなければ HTTP を用いる

if (NOT DEFINED AZURE_IOT_PROTOCOL)
  message(STATUS "Set \"http\" as AZURE_IOT_PROTOCOL")
  set(AZURE_IOT_PROTOCOL http)
endif ()

option(with_test "Build with test program" OFF)

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
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DEPEND_LIB_ROOT ${PROJECT_BINARY_DIR}/dep)
#link_directories(${PROJECT_BINARY_DIR})
include(GNUInstallDirs)
link_directories(${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR})

add_library(${PROJECT_NAME}_amqp SHARED iothub_amqp.cpp)
add_library(${PROJECT_NAME}_mqtt SHARED iothub_mqtt.cpp)
add_library(${PROJECT_NAME}_http SHARED iothub_http.cpp)

target_include_directories(${PROJECT_NAME}_amqp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${DEPEND_LIB_ROOT}/include)
target_include_directories(${PROJECT_NAME}_mqtt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${DEPEND_LIB_ROOT}/include)
target_include_directories(${PROJECT_NAME}_http PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${DEPEND_LIB_ROOT}/include)

include(ExternalProject)

#### azure-iot-sdk
set(AZIOT_REPO https://github.com/Azure/azure-iot-sdk-c)
set(AZIOT_TAG lts_07_2017)
set(AZIOT_SRC ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c)
# サブモジュールの再帰的取得は古いCMakeでは行ってくれない
if (CMAKE_VERSION VERSION_LESS "3.0")
  ExternalProject_Add(azure-iot-sdk-c
    GIT_REPOSITORY ${AZIOT_REPO}
    GIT_TAG ${AZIOT_TAG}
    UPDATE_COMMAND git submodule update --init --recursive
    INSTALL_DIR ${DEPEND_LIB_ROOT}
    SOURCE_DIR ${AZIOT_SRC}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${DEPEND_LIB_ROOT})
else()
  ExternalProject_Add(azure-iot-sdk-c
    GIT_REPOSITORY ${AZIOT_REPO}
    GIT_TAG ${AZIOT_TAG}
    INSTALL_DIR ${DEPEND_LIB_ROOT}
    SOURCE_DIR ${AZIOT_SRC}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${DEPEND_LIB_ROOT})
endif()

# cert.c コンパイル用
target_include_directories(${PROJECT_NAME}_amqp PRIVATE ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c/certs)
target_include_directories(${PROJECT_NAME}_mqtt PRIVATE ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c/certs)
target_include_directories(${PROJECT_NAME}_http PRIVATE ${DEPEND_LIB_ROOT}/src/azure-iot-sdk-c/certs)

add_dependencies(${PROJECT_NAME}_amqp azure-iot-sdk-c)
add_dependencies(${PROJECT_NAME}_mqtt azure-iot-sdk-c)
add_dependencies(${PROJECT_NAME}_http azure-iot-sdk-c)

target_link_libraries(${PROJECT_NAME}_amqp INTERFACE
  ssl crypto curl uuid
  aziotsharedutil iothub_client_amqp_transport iothub_client_amqp_ws_transport uamqp)
target_link_libraries(${PROJECT_NAME}_mqtt INTERFACE
  ssl crypto curl uuid
  aziotsharedutil iothub_client_mqtt_transport iothub_client_mqtt_ws_transport umqtt)
target_link_libraries(${PROJECT_NAME}_http INTERFACE
  ssl crypto curl uuid
  aziotsharedutil iothub_client_http_transport)

set_target_properties(${PROJECT_NAME}_amqp PROPERTIES
  PUBLIC_HEADER "iothub.hpp" VERSION ${VERSION} SOVERSION ${VER_MAJOR})
set_target_properties(${PROJECT_NAME}_mqtt PROPERTIES
  VERSION ${VERSION} SOVERSION ${VER_MAJOR})
set_target_properties(${PROJECT_NAME}_http PROPERTIES
  VERSION ${VERSION} SOVERSION ${VER_MAJOR})

#### installation
install(TARGETS ${PROJECT_NAME}_amqp
 LIBRARY DESTINATION lib
 PUBLIC_HEADER  DESTINATION include/azure)
install(TARGETS ${PROJECT_NAME}_mqtt LIBRARY DESTINATION lib)
install(TARGETS ${PROJECT_NAME}_http LIBRARY DESTINATION lib)

## test
if (${with_test})
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

  add_dependencies(${TEST_NAME} argagg ${PROJECT_NAME}_amqp)
  target_link_libraries(${TEST_NAME} pthread ${PROJECT_NAME})
endif()
