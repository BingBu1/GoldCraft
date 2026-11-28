cmake_minimum_required(VERSION 3.20)
project(scp682_bake LANGUAGES CXX)
add_library(raster SHARED raster.cpp)
target_compile_features(raster PRIVATE cxx_std_17)
