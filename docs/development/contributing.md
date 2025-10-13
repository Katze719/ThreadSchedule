# Contributing to ThreadSchedule

Thank you for your interest in contributing to ThreadSchedule! This guide will help you get started.

## Code of Conduct

Be respectful and constructive in all interactions. We aim to foster an inclusive and welcoming community.

## Getting Started

### 1. Fork and Clone

```bash
# Fork on GitHub, then clone your fork
git clone https://github.com/YOUR_USERNAME/ThreadSchedule.git
cd ThreadSchedule

# Add upstream remote
git remote add upstream https://github.com/Katze719/ThreadSchedule.git
```

### 2. Create a Branch

```bash
# Create feature branch
git checkout -b feature/amazing-feature

# Or bugfix branch
git checkout -b fix/issue-123
```

### 3. Set Up Development Environment

```bash
# Build with all features enabled
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DTHREADSCHEDULE_BUILD_EXAMPLES=ON \
    -DTHREADSCHEDULE_BUILD_TESTS=ON \
    -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build .
```

## Development Workflow

### 1. Make Changes

- Keep changes focused and atomic
- Follow existing code style
- Add tests for new features
- Update documentation

### 2. Write Tests

```cpp
// tests/test_my_feature.cpp
#include <gtest/gtest.h>
#include <threadschedule/threadschedule.hpp>

using namespace threadschedule;

TEST(MyFeatureTest, BasicFunctionality) {
    // Arrange
    MyFeature feature;
    
    // Act
    auto result = feature.do_something();
    
    // Assert
    EXPECT_EQ(result, expected_value);
}

TEST(MyFeatureTest, EdgeCases) {
    // Test edge cases
}
```

### 3. Run Tests

```bash
cd build
ctest --output-on-failure

# Or run specific test
./tests/my_feature_tests
```

### 4. Check Code Quality

```bash
# Format code (if clang-format is available)
clang-format -i include/threadschedule/*.hpp

# Run static analysis (if clang-tidy is available)
clang-tidy include/threadschedule/*.hpp
```

### 5. Commit Changes

```bash
# Stage changes
git add .

# Commit with clear message
git commit -m "Add feature: description of feature

- Detail 1
- Detail 2
- Fixes #123"
```

### 6. Push and Create PR

```bash
# Push to your fork
git push origin feature/amazing-feature

# Create Pull Request on GitHub
```

## Coding Standards

### Code Style

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 120 characters max
- **Naming conventions**:
  - Classes: `PascalCase`
  - Functions: `snake_case`
  - Variables: `snake_case`
  - Constants: `UPPER_CASE`
  - Private members: `snake_case_` (trailing underscore)

### Example

```cpp
class ThreadWrapper {
public:
    // Public interface
    void set_name(const std::string& name);
    std::string get_name() const;
    
private:
    // Private members with trailing underscore
    std::thread thread_;
    std::string name_;
    std::mutex mutex_;
    
    // Private helper
    void configure_thread_properties();
};
```

### Headers

```cpp
// Include guards
#pragma once

// System headers first
#include <string>
#include <thread>
#include <mutex>

// Then project headers
#include "other_header.hpp"

// Namespace
namespace threadschedule {

// Your code here

}  // namespace threadschedule
```

### Comments

```cpp
/**
 * @brief Brief description of the class
 * 
 * Detailed description with usage examples.
 */
class MyClass {
public:
    /**
     * @brief Brief description of the method
     * @param name The name parameter
     * @return Description of return value
     * @throws std::runtime_error If something goes wrong
     */
    std::string do_something(const std::string& name);
};
```

## Testing Guidelines

### Test Structure

```cpp
TEST(ComponentTest, FeatureUnderTest) {
    // Arrange - Set up test
    ThreadWrapper worker("test", []() {});
    
    // Act - Execute the test
    worker.set_name("NewName");
    
    // Assert - Verify results
    EXPECT_EQ(worker.get_name(), "NewName");
    
    // Cleanup if needed
    worker.join();
}
```

### Test Coverage

- **Unit tests**: Test individual components
- **Integration tests**: Test component interactions
- **Edge cases**: Test boundary conditions
- **Error cases**: Test error handling

### Platform-Specific Tests

```cpp
#ifdef __linux__
TEST(PThreadWrapperTest, LinuxSpecificFeature) {
    // Linux-only test
}
#endif

#ifdef _WIN32
TEST(ThreadWrapperTest, WindowsSpecificFeature) {
    // Windows-only test
}
#endif
```

## Documentation

### API Documentation

Use Doxygen-style comments:

```cpp
/**
 * @brief Set thread CPU affinity
 * 
 * Pins the thread to specific CPU cores.
 * 
 * @param cpus Vector of CPU core IDs
 * @throws std::system_error If affinity cannot be set
 * 
 * @note Linux: Uses sched_setaffinity
 * @note Windows: Uses SetThreadAffinityMask
 * 
 * @example
 * @code
 * ThreadWrapper worker("MyWorker", []() {});
 * worker.set_affinity({0, 1});  // Pin to cores 0 and 1
 * @endcode
 */
void set_affinity(const std::vector<int>& cpus);
```

### User Documentation

Add examples to docs/:

- Update existing guides
- Add new guides for new features
- Include code examples
- Explain use cases

## Pull Request Guidelines

### PR Description Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
- [ ] Unit tests added/updated
- [ ] Integration tests pass
- [ ] Manual testing performed

## Checklist
- [ ] Code follows project style
- [ ] Comments added where needed
- [ ] Documentation updated
- [ ] Tests pass locally
- [ ] No compiler warnings
```

### PR Best Practices

1. **Keep PRs focused**: One feature or fix per PR
2. **Write clear descriptions**: Explain what and why
3. **Link issues**: Reference related issues
4. **Respond to feedback**: Address review comments promptly
5. **Keep commits clean**: Squash if needed

## Review Process

### What Reviewers Look For

- **Correctness**: Does it work as intended?
- **Tests**: Are there adequate tests?
- **Code quality**: Is it readable and maintainable?
- **Documentation**: Is it documented?
- **Performance**: Are there performance implications?
- **Compatibility**: Does it work on all platforms?

### Addressing Feedback

```bash
# Make changes based on feedback
git add .
git commit -m "Address review feedback"

# Push updates
git push origin feature/amazing-feature
```

## Common Tasks

### Adding a New Feature

1. Create issue describing the feature
2. Discuss design in issue comments
3. Implement feature with tests
4. Update documentation
5. Submit PR

### Fixing a Bug

1. Write test that reproduces bug
2. Fix the bug
3. Verify test passes
4. Submit PR with issue reference

### Updating Documentation

1. Make documentation changes
2. Build docs locally to verify
3. Submit PR

## Building Documentation

### Install Dependencies

```bash
pip install mkdocs mkdocs-material mkdoxy
```

### Build and Preview

```bash
# Build documentation
mkdocs build

# Serve locally
mkdocs serve

# Open http://localhost:8000
```

## Release Process

For maintainers:

1. Update VERSION file
2. Update CHANGELOG.md
3. Create release branch
4. Tag release
5. Build and test
6. Create GitHub release
7. Update documentation

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/Katze719/ThreadSchedule/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Katze719/ThreadSchedule/discussions)
- **Documentation**: [Full docs](https://katze719.github.io/ThreadSchedule/)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

## Recognition

Contributors will be acknowledged in:
- CONTRIBUTORS.md file
- Release notes
- Project README

Thank you for contributing to ThreadSchedule! 🎉
