from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, load

import os


class ThreadScheduleConan(ConanFile):
    name = "threadschedule"
    package_type = "library"
    license = "MIT"
    author = "Katze719"
    url = "https://github.com/Katze719/ThreadSchedule"
    homepage = "https://katze719.github.io/ThreadSchedule/"
    description = "C++17 thread management and scheduling library"
    topics = (
        "threading",
        "concurrency",
        "thread-pool",
        "scheduling",
        "header-only",
    )

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }
    exports_sources = (
        "CMakeLists.txt",
        "VERSION",
        "LICENSE",
        "cmake/*",
        "include/*",
        "src/*",
    )

    def set_version(self):
        self.version = load(
            self, os.path.join(self.recipe_folder, "VERSION")
        ).strip()

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def package_id(self):
        if not self.info.options.shared:
            self.info.settings.rm_safe("build_type")
            self.info.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["THREADSCHEDULE_RUNTIME"] = bool(
            self.options.shared
        )
        toolchain.variables["THREADSCHEDULE_BUILD_EXAMPLES"] = False
        toolchain.variables["THREADSCHEDULE_BUILD_TESTS"] = False
        toolchain.variables["THREADSCHEDULE_BUILD_BENCHMARKS"] = False
        toolchain.variables["THREADSCHEDULE_BUILD_DOCS"] = False
        toolchain.variables["THREADSCHEDULE_INSTALL"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ThreadSchedule")

        headers = self.cpp_info.components["ThreadSchedule"]
        headers.set_property(
            "cmake_target_name", "ThreadSchedule::ThreadSchedule"
        )
        headers.includedirs = ["include"]
        if self.settings.os == "Linux":
            headers.system_libs = ["pthread", "rt"]

        if self.options.shared:
            runtime = self.cpp_info.components["Runtime"]
            runtime.set_property("cmake_target_name", "ThreadSchedule::Runtime")
            runtime.libs = [
                "threadscheduled"
                if str(self.settings.build_type) == "Debug"
                else "threadschedule"
            ]
            runtime.requires = ["ThreadSchedule"]
            runtime.defines = ["THREADSCHEDULE_RUNTIME"]
