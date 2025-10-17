from conan import ConanFile, tools
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.files import copy
import os


class ThreadScheduleConan(ConanFile):
    name = "threadschedule"
    license = "MIT"
    author = "Katze719"
    url = "https://github.com/Katze719/ThreadSchedule"
    description = "Modern C++ thread management library for Linux and Windows"
    exports_sources = "CMakeLists.txt", "src/*", "include/*"
    topics = (
        "threading",
        "concurrency",
        "thread-pool",
        "cpp17",
        "cpp20",
        "cpp23",
        "header-only",
    )

    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared_runtime": [True, False],
        "build_examples": [True, False],
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
    }

    default_options = {
        "shared_runtime": True,
        "build_examples": False,
        "build_tests": False,
        "build_benchmarks": False,
    }

    # Header-only by default, but can build shared runtime
    no_copy_source = False

    def export_sources(self):
        # Export sources from parent directory (project root)
        parent_dir = os.path.join(self.recipe_folder, "..")
        copy(self, "CMakeLists.txt", src=parent_dir, dst=self.export_sources_folder)
        copy(self, "VERSION", src=parent_dir, dst=self.export_sources_folder)
        copy(self, "LICENSE", src=parent_dir, dst=self.export_sources_folder)
        copy(
            self,
            "*.hpp",
            src=os.path.join(parent_dir, "include"),
            dst=os.path.join(self.export_sources_folder, "include"),
            keep_path=True,
        )
        copy(
            self,
            "*.h",
            src=os.path.join(parent_dir, "include"),
            dst=os.path.join(self.export_sources_folder, "include"),
            keep_path=True,
        )
        copy(
            self,
            "*.cpp",
            src=os.path.join(parent_dir, "src"),
            dst=os.path.join(self.export_sources_folder, "src"),
            keep_path=True,
        )
        copy(
            self,
            "*.cmake*",
            src=os.path.join(parent_dir, "cmake"),
            dst=os.path.join(self.export_sources_folder, "cmake"),
            keep_path=True,
        )
        copy(
            self,
            "*.in",
            src=os.path.join(parent_dir, "cmake"),
            dst=os.path.join(self.export_sources_folder, "cmake"),
            keep_path=True,
        )
        # Optional directories (may not exist in all cases)
        if os.path.exists(os.path.join(parent_dir, "examples")):
            copy(
                self,
                "*",
                src=os.path.join(parent_dir, "examples"),
                dst=os.path.join(self.export_sources_folder, "examples"),
                keep_path=True,
            )
        if os.path.exists(os.path.join(parent_dir, "tests")):
            copy(
                self,
                "*",
                src=os.path.join(parent_dir, "tests"),
                dst=os.path.join(self.export_sources_folder, "tests"),
                keep_path=True,
            )
        if os.path.exists(os.path.join(parent_dir, "benchmarks")):
            copy(
                self,
                "*",
                src=os.path.join(parent_dir, "benchmarks"),
                dst=os.path.join(self.export_sources_folder, "benchmarks"),
                keep_path=True,
            )

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
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        # Set include directory
        self.cpp_info.includedirs = ["include"]

        if self.options.shared_runtime:
            # Shared runtime mode - provides both header and runtime library
            self.cpp_info.libs = ["threadschedule"]

            # Define both components
            self.cpp_info.components["ThreadSchedule"].includedirs = ["include"]
            self.cpp_info.components["ThreadSchedule"].set_property(
                "cmake_target_name", "ThreadSchedule::ThreadSchedule"
            )

            self.cpp_info.components["Runtime"].libs = ["threadschedule"]
            self.cpp_info.components["Runtime"].includedirs = ["include"]
            self.cpp_info.components["Runtime"].set_property(
                "cmake_target_name", "ThreadSchedule::Runtime"
            )

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
        self.cpp_info.set_property(
            "cmake_target_name", "ThreadSchedule::ThreadSchedule"
        )
        self.cpp_info.set_property(
            "cmake_target_aliases", ["ThreadSchedule::ThreadSchedule"]
        )

    def package_id(self):
        # Shared runtime mode - package_id depends on settings (default behavior)
        pass

    generators = "cmake_find_package_multi"
    git_url = url + ".git"
    install_folder = "temp_install"
    info_file_extension = "_info.json"

    @property
    def info_file_name(self):
        return self.name + self.info_file_extension

    @property
    def given_channel(self):
        try:
            return self.channel
        except:
            return None

    @property
    def given_user(self):
        try:
            return self.user
        except:
            return None

    @property
    def given_version(self):
        try:
            return self.version
        except:
            return None

    @property
    def meap_channel(self):
        # If on branch or release, the same "version@user/channel" should be used
        if (
            self.given_channel != None
            and self.given_user != None
            and self.given_version != None
        ):
            if self.given_channel == "branch" or self.given_channel == "release":
                return (
                    str(self.given_version)
                    + "@"
                    + str(self.given_user)
                    + "/"
                    + str(self.given_channel)
                )
        return "latest@me/main"

    # Initialize the package
    def init(self):
        self.output.info(
            self.name + " - Given: " + str(self.version) + "@" + str(self.given_channel)
        )
        self.version = get_channel_version(str(self.given_channel), self.version)
        self.output.info(
            self.name + " - Using: " + str(self.version) + "@" + str(self.given_channel)
        )

    def get_info_dict(self):
        info_dict = {"url": self.url}
        try:
            git = tools.Git(self.name)
            info_dict = {
                "url": self.url,
                "git-url": self.git_url,
                "git-sha": git.get_revision(),
                "git-branch": git.get_branch(),
                "git-described": git.run("describe --tag --dirty --long"),
                "gitlab-jobId": (
                    os.environ.get("CI_JOB_ID")
                    if os.environ.get("CI_JOB_ID") is not None
                    else ""
                ),
                "gitlab-url-job": (
                    os.environ.get("CI_JOB_URL")
                    if os.environ.get("CI_JOB_URL") is not None
                    else ""
                ),
                "gitlab-url-commit": (self.url + "/-/commit/" + git.get_revision()),
            }
        except:
            return "unknown"

        return info_dict


## ------------------------------------ Common ME functions
def get_version():
    git = tools.Git()
    try:
        tagStr = git.run("describe --tags --abbrev=0")
        if tagStr == "":
            raise Exception("No git tag found")
        return tagStr.replace("v", "")
    except:
        return "0.0.1"


def get_commit_id():
    git = tools.Git()
    try:
        commit_id = str((git.run("rev-parse HEAD")))
        if commit_id == "":
            raise Exception("No git commit id found")
        return commit_id[0:8]
    except:
        return "unknown"


def get_branch():
    git = tools.Git()
    try:
        branch_name = git.get_branch()
        if branch_name.startswith("HEAD"):
            return None
        return branch_name.replace("/", "-")
    except:
        return "unknown"


def get_channel_version(channel: str, version: str):
    if channel == "release" and version == None:
        return get_version()
    if channel == "branch" and version == None:
        return get_branch()
    if channel == "git" and version == None:
        return get_commit_id()
    return "latest"
