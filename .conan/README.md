# Conan Package Definition

This directory contains the Conan package definition for ThreadSchedule.

## Building the Package

To create a Conan package from the repository root:

```bash
conan create .conan --build=missing
```

## Configuration Options

The conanfile.py supports the following options:

- `shared_runtime`: Build as shared runtime library (default: False)
- `build_examples`: Build example programs (default: False)
- `build_tests`: Build unit tests (default: False)
- `build_benchmarks`: Build benchmarks (default: False)

### Example Usage

Header-only mode (default):
```bash
conan create .conan --build=missing
```

With shared runtime:
```bash
conan create .conan --build=missing -o shared_runtime=True
```

With all options enabled:
```bash
conan create .conan --build=missing -o shared_runtime=True -o build_examples=True -o build_tests=True -o build_benchmarks=True
```

## CI Testing

The GitHub Actions workflow `.github/workflows/conan-package.yml` tests all configurations to ensure the package builds correctly on Linux and Windows.
