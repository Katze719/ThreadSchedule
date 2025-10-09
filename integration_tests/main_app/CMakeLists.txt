cmake_minimum_required(VERSION 3.14)
project(MainApp LANGUAGES CXX)

# Find dependencies
find_package(ThreadSchedule REQUIRED)
find_package(LibraryA REQUIRED)
find_package(LibraryB REQUIRED)

# Create the main application
add_executable(main_app
    main.cpp
)

target_link_libraries(main_app
    PRIVATE
        ThreadSchedule::ThreadSchedule
        LibraryA::library_a
        LibraryB::library_b
)

# Set C++ standard
target_compile_features(main_app PRIVATE cxx_std_17)

# Add test
enable_testing()
add_test(NAME RegistryIntegrationTest COMMAND main_app)
