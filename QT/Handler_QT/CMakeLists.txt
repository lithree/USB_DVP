cmake_minimum_required(VERSION 3.16)

project(Handler_QT VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(${PROJECT_NAME}
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libusb-1.0.a")
    set(LIBUSB_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/libusb-1.0.a")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libusb-1.0.a")
    set(LIBUSB_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/lib/libusb-1.0.a")
else()
    find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 REQUIRED)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${LIBUSB_LIBRARY}
)
