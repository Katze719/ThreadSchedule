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
        "shared_runtime": [True, False],
        "build_examples": [True, False],
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
    }
    
    default_options = {
        "shared_runtime": False,
        "build_examples": False,
        "build_tests": False,
        "build_benchmarks": False,
    }
    
    # Header-only by default, but can build shared runtime
    no_copy_source = False
    
    def set_version(self):
        # Resolve version from VERSION file in parent directory if not provided
        if not self.version:
            version_file = os.path.join(self.recipe_folder, "..", "VERSION")
            if os.path.exists(version_file):
                with open(version_file, "r", encoding="utf-8") as f:
                    self.version = f.read().strip()
            else:
                self.version = "0.0.0"
    
    def export_sources(self):
        # Export sources from parent directory (project root)
        parent_dir = os.path.join(self.recipe_folder, "..")
        copy(self, "CMakeLists.txt", src=parent_dir, dst=self.export_sources_folder)
        copy(self, "VERSION", src=parent_dir, dst=self.export_sources_folder)
        copy(self, "LICENSE", src=parent_dir, dst=self.export_sources_folder)
        copy(self, "*.hpp", src=os.path.join(parent_dir, "include"), dst=os.path.join(self.export_sources_folder, "include"))
        copy(self, "*.cpp", src=os.path.join(parent_dir, "src"), dst=os.path.join(self.export_sources_folder, "src"), keep_path=True)
        copy(self, "*.cmake*", src=os.path.join(parent_dir, "cmake"), dst=os.path.join(self.export_sources_folder, "cmake"))
        copy(self, "*.cpp", src=os.path.join(parent_dir, "examples"), dst=os.path.join(self.export_sources_folder, "examples"), keep_path=True)
        copy(self, "*.cmake", src=os.path.join(parent_dir, "examples"), dst=os.path.join(self.export_sources_folder, "examples"), keep_path=True)
        copy(self, "*.cpp", src=os.path.join(parent_dir, "tests"), dst=os.path.join(self.export_sources_folder, "tests"), keep_path=True)
        copy(self, "*.cmake", src=os.path.join(parent_dir, "tests"), dst=os.path.join(self.export_sources_folder, "tests"), keep_path=True)
        copy(self, "*.cpp", src=os.path.join(parent_dir, "benchmarks"), dst=os.path.join(self.export_sources_folder, "benchmarks"), keep_path=True)
        copy(self, "*.cmake", src=os.path.join(parent_dir, "benchmarks"), dst=os.path.join(self.export_sources_folder, "benchmarks"), keep_path=True)
    
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
        tc = CMakeToolchain(self)
        tc.variables["THREADSCHEDULE_RUNTIME"] = self.options.shared_runtime
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
        # Set include directory
        self.cpp_info.includedirs = ["include"]
        
        if self.options.shared_runtime:
            # Shared runtime mode - provides both header and runtime library
            self.cpp_info.libs = ["threadschedule"]
            
            # Define both components
            self.cpp_info.components["ThreadSchedule"].includedirs = ["include"]
            self.cpp_info.components["ThreadSchedule"].set_property("cmake_target_name", "ThreadSchedule::ThreadSchedule")
            
            self.cpp_info.components["Runtime"].libs = ["threadschedule"]
            self.cpp_info.components["Runtime"].includedirs = ["include"]
            self.cpp_info.components["Runtime"].set_property("cmake_target_name", "ThreadSchedule::Runtime")
            
            # System libraries
            if self.settings.os == "Linux":
                self.cpp_info.components["Runtime"].system_libs = ["pthread", "rt"]
        else:
            # Header-only mode
            self.cpp_info.bindirs = []
            self.cpp_info.libdirs = []
            
            # System libraries for header-only
            if self.settings.os == "Linux":
                self.cpp_info.system_libs = ["pthread", "rt"]
            elif self.settings.os == "Windows":
                pass  # Windows uses standard thread library
        
        # Set the main target name
        self.cpp_info.set_property("cmake_file_name", "ThreadSchedule")
        self.cpp_info.set_property("cmake_target_name", "ThreadSchedule::ThreadSchedule")
        self.cpp_info.set_property("cmake_target_aliases", ["ThreadSchedule::ThreadSchedule"])

    def package_id(self):
        # Header-only mode - package_id doesn't depend on settings
        if not self.options.shared_runtime:
            self.info.clear()
        # Shared runtime mode - package_id depends on settings (default behavior)
