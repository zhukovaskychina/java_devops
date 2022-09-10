cmake_minimum_required(VERSION 3.17)
project(devops)

set(CMAKE_CXX_STANDARD 14)

include_directories(${JAVA_INCLUDE_PATH} /Library/Java/JavaVirtualMachines/jdk1.8.0_212.jdk/Contents/Home/include)
include_directories(${JAVA_INCLUDE_PATH2} /Library/Java/JavaVirtualMachines/jdk1.8.0_212.jdk/Contents/Home/include/darwin)

SET(LIB_SRC library.cpp)
ADD_LIBRARY(devops SHARED ${LIB_SRC})