from MEBaseConan import MEBaseConan
from conans import CMake


class ThreadScheduleConan(MEBaseConan):
    name = "threadschedule"
    cmake_name = "threadschedule"
    license = "MIT"
    author = "Katze719"
    url = "https://gitlab02.micro-epsilon.de/me/ext/ThreadSchedule"
    description = "Modern C++ thread management library for Linux and Windows"
    topics = ("threading", "concurrency", "thread-pool", "cpp17", "cpp20", "cpp23", "cpp26", "header-only", "modules")
    options = {
        **MEBaseConan.options,       
        "shared_runtime": [True, False],
        "cpp_module": [True, False],
        "build_examples": [True, False],
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
    }
    default_options = {
        **MEBaseConan.default_options,
        "shared_runtime": True,
        "cpp_module": False,
        "build_examples": False,
        "build_tests": False,
        "build_benchmarks": False,
    }

    def _configure_cmake(self):
        cmake = super()._configure_cmake()
        cmake.definitions["THREADSCHEDULE_RUNTIME"] = "ON" if self.options.shared_runtime else "OFF"
        cmake.definitions["THREADSCHEDULE_MODULE"] = "ON" if self.options.cpp_module else "OFF"
        cmake.definitions["THREADSCHEDULE_BUILD_EXAMPLES"] = "ON" if self.options.build_examples else "OFF"
        cmake.definitions["THREADSCHEDULE_BUILD_TESTS"] = "ON" if self.options.build_tests else "OFF"
        cmake.definitions["THREADSCHEDULE_BUILD_BENCHMARKS"] = "ON" if self.options.build_benchmarks else "OFF"
        cmake.definitions["THREADSCHEDULE_INSTALL"] = "ON" 
        cmake.definitions["THREADSCHEDULE_BUILD_DOCS"] = "OFF"
        return cmake
