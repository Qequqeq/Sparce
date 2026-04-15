cmake_minimum_required(VERSION 4.0)
project(SparceMatrixFPGA)

set(CMAKE_CXX_STANDARD 20)

add_executable(SparceMatrixFPGA main.cpp
        CSRMatrix.cpp
        CSRMatrix.h)
