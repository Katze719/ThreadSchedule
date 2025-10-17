from MEBaseConan import MEBaseConan


class ThreadScheduleConan(MEBaseConan):
    name = "threadschedule"
    license = "MIT"
    author = "Katze719"
    url = "https://gitlab02.micro-epsilon.de/me/ext/ThreadSchedule"
    description = "Modern C++ thread management library for Linux and Windows"
    topics = (
        "threading",
        "concurrency",
        "thread-pool",
        "cpp17",
        "cpp20",
        "cpp23",
        "header-only",
    )

    options = {
        "shared_runtime": [True, False],
        "build_examples": [True, False],
        "build_tests": [True, False],
        "build_benchmarks": [True, False],
        "shared": [True, False],
    }

    default_options = {
        "shared_runtime": True,
        "build_examples": False,
        "build_tests": False,
        "build_benchmarks": False,
        "shared": True,
    }

    def configure(self):
        self.options["package"].shared = self.options.shared
