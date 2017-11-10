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
#link_directories(${PROJECT_BINARY_DIR})
include(GNUInstallDirs)
link_directories(${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR})

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

# 以下で行う依存ライブラリ結合の準備
set(LIBFILE ${PROJECT_BINARY_DIR}/libazure_iothub.a)
set(TMPLIBFILE ${PROJECT_BINARY_DIR}/libazure_iothub_tmp.a)

if (${AZURE_IOT_PROTOCOL} STREQUAL amqp)
  add_definitions(-DAZURE_IOT_AMQP)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND mv ${LIBFILE} ${TMPLIBFILE}
    COMMAND ${CMAKE_AR} cqT ${LIBFILE}
    ${TMPLIBFILE}
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libaziotsharedutil.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client_amqp_transport.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client_amqp_ws_transport.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libuamqp.a
    COMMAND echo -e \"create ${LIBFILE}\\naddlib ${LIBFILE}\\nsave\\nend\\n\" | ${CMAKE_AR} -M
    COMMAND rm ${TMPLIBFILE})
elseif (${AZURE_IOT_PROTOCOL} STREQUAL mqtt)
  add_definitions(-DAZURE_IOT_MQTT)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND mv ${LIBFILE} ${TMPLIBFILE}
    COMMAND ${CMAKE_AR} cqT ${LIBFILE}
    ${TMPLIBFILE}
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libaziotsharedutil.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client_mqtt_transport.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client_mqtt_ws_transport.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libumqtt.a
    COMMAND echo -e \"create ${LIBFILE}\\naddlib ${LIBFILE}\\nsave\\nend\\n\" | ${CMAKE_AR} -M
    COMMAND rm ${TMPLIBFILE})
elseif (${AZURE_IOT_PROTOCOL} STREQUAL http)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND mv ${LIBFILE} ${TMPLIBFILE}
    COMMAND ${CMAKE_AR} cqT ${LIBFILE}
    ${TMPLIBFILE}
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libaziotsharedutil.a
    ${DEPEND_LIB_ROOT}/${CMAKE_INSTALL_LIBDIR}/libiothub_client_http_transport.a
    COMMAND echo -e \"create ${LIBFILE}\\naddlib ${LIBFILE}\\nsave\\nend\\n\" | ${CMAKE_AR} -M
    COMMAND rm ${TMPLIBFILE})
elseif (DEFINED ${AZURE_IOT_PROTOCOL}) # いずれでもないものが含まれている
  message(FATAL_ERROR "invalid protocol")
endif()


target_link_libraries(${PROJECT_NAME} INTERFACE
  ssl
  crypto
  curl
  uuid
  )

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "iothub.hpp")
INSTALL(TARGETS ${PROJECT_NAME}
  INCLUDES DESTINATION include
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  PUBLIC_HEADER  DESTINATION include/azure)

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

add_dependencies(${TEST_NAME} argagg ${PROJECT_NAME})
target_link_libraries(${TEST_NAME} pthread ${PROJECT_NAME})
