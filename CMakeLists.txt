cmake_minimum_required(VERSION 2.8.12)

project(ATRUSCTL CXX)

add_compile_options(-std=gnu++11 -Wall -Wformat=2 -Werror -fvisibility=internal -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables)

add_subdirectory(src)
