from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.files import copy
import os


class ThreadScheduleConan(ConanFile):
    name = "threadschedule"
    # Version will be resolved from VERSION file or env/recipe override
    version = None
    license = "MIT"
    author = "Katze719"
    url = "https://github.com/Katze719/ThreadSchedule"
    description = "Modern C++ thread management library for Linux and Windows"
    topics = ("threading", "concurrency", "thread-pool", "cpp17", "cpp20", "cpp23", "header-only")
    
    settings = "os", "compiler", "build_type", "arch"
    
    options = {
        "build_examples": [True, False],
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
    }
    
    default_options = {
        "build_examples": False,
        "build_tests": False,
        "build_benchmarks": False,
    }
    
    # Header-only library - no sources
    no_copy_source = True
    
    def requirements(self):
        # No runtime requirements for header-only library
        pass
    
    def build_requirements(self):
        # Only add benchmark dependency if building benchmarks
        if self.options.build_benchmarks:
            self.tool_requires("benchmark/1.9.4")
    
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        # Resolve version from VERSION file if not provided by recipe/CLI
        if not self.version:
            version_file = os.path.join(self.recipe_folder, "VERSION")
            if os.path.exists(version_file):
                with open(version_file, "r", encoding="utf-8") as f:
                    self.version = f.read().strip()
            else:
                self.version = "0.0.0"
        tc = CMakeToolchain(self)
        tc.variables["THREADSCHEDULE_BUILD_EXAMPLES"] = self.options.build_examples
        tc.variables["THREADSCHEDULE_BUILD_TESTS"] = self.options.build_tests
        tc.variables["THREADSCHEDULE_BUILD_BENCHMARKS"] = self.options.build_benchmarks
        tc.variables["THREADSCHEDULE_INSTALL"] = True
        tc.generate()
        
        deps = CMakeDeps(self)
        deps.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    
    def package(self):
        cmake = CMake(self)
        cmake.install()
        
        # Copy license
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
    
    def package_info(self):
        # Header-only library
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        
        # Set include directory
        self.cpp_info.includedirs = ["include"]
        
        # System libraries
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["pthread", "rt"]
        elif self.settings.os == "Windows":
            pass  # Windows uses standard thread library
        
        # Set the target name
        self.cpp_info.set_property("cmake_file_name", "ThreadSchedule")
        self.cpp_info.set_property("cmake_target_name", "ThreadSchedule::ThreadSchedule")
        
        # Ensure C++17 minimum
        self.cpp_info.set_property("cmake_target_aliases", ["ThreadSchedule::ThreadSchedule"])

    def package_id(self):
        # Header-only library - package_id doesn't depend on settings
        self.info.clear()
