# GitHub Actions Workflows

This directory contains GitHub Actions workflows for automated CI/CD, testing, and deployment.

## Workflows

### 1. CI (`ci.yml`)

**Triggers**: Push, Pull Request, Manual

Comprehensive continuous integration workflow that:
- **Linux builds**: Tests on Ubuntu 22.04 and 24.04 with GCC and Clang
- **Windows builds**: Tests with MSVC compiler
- **C++ standards**: Tests C++17, C++20, and C++23 compatibility
- **Integration tests**: Validates subdirectory, FetchContent, and CPM integration methods
- **Code quality**: Checks code formatting with clang-format

**Status Badge**:
```markdown
[![CI](https://github.com/Katze719/ThreadSchedule/workflows/CI/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
```

### 2. Conan Deploy (`conan-deploy.yml`)

**Triggers**: Release published, Manual

Handles Conan package creation and deployment:
- **Multi-platform**: Builds on Linux and Windows
- **Multiple configurations**: Tests C++17, 20, and 23
- **Package testing**: Validates the Conan package works correctly
- **Deployment**: Uploads to Conan remote (requires secrets)
- **Release artifacts**: Creates source and header-only archives

**Required Secrets** (for custom Conan remote):
- `CONAN_REMOTE_URL`: Your Conan remote URL
- `CONAN_LOGIN_USERNAME`: Conan username
- `CONAN_PASSWORD`: Conan password
- `CONAN_USERNAME`: Organization/user name (optional, defaults to 'threadschedule')

**Manual Trigger**:
Go to Actions → Conan Deploy → Run workflow
- Enter version (e.g., `1.0.0`)
- Select channel (default: `stable`)

**Status Badge**:
```markdown
[![Conan Deploy](https://github.com/Katze719/ThreadSchedule/workflows/Conan%20Deploy/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/conan-deploy.yml)
```

### 3. Documentation (`documentation.yml`)

**Triggers**: Push to main, Pull Request

Validates documentation quality:
- **Link checking**: Verifies all markdown links are valid
- **README validation**: Ensures examples are present
- **Integration guide**: Validates INTEGRATION_NEW.md completeness
- **CMake reference**: Checks CMAKE_REFERENCE.md documentation

**Status Badge**:
```markdown
[![Documentation](https://github.com/Katze719/ThreadSchedule/workflows/Documentation/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
```

## Setting Up Workflows

### 1. Enable Actions

Actions are automatically enabled for public repositories. For private repositories:
1. Go to Settings → Actions → General
2. Enable "Allow all actions and reusable workflows"

### 2. Configure Secrets (for Conan deployment)

For automated Conan deployment, add these secrets:
1. Go to Settings → Secrets and variables → Actions
2. Add the following repository secrets:
   - `CONAN_REMOTE_URL`: Your Conan server URL
   - `CONAN_LOGIN_USERNAME`: Your Conan username
   - `CONAN_PASSWORD`: Your Conan password

Example for ConanCenter (no secrets needed, manual PR process):
```bash
# After release, create a PR at:
# https://github.com/conan-io/conan-center-index
```

### 3. Branch Protection

Recommended branch protection for `main`:
1. Go to Settings → Branches → Add rule
2. Branch name pattern: `main`
3. Enable:
   - Require status checks to pass before merging
   - Require branches to be up to date before merging
   - Select required status checks:
     - `Linux (ubuntu-latest, C++17, gcc)`
     - `Windows (C++17, msvc)`
     - `Integration Test (subdirectory)`

## CI/CD Pipeline

### Pull Request Flow

```
PR Created/Updated
    ↓
CI Workflow Runs
    ├─ Build on Linux (multiple configurations)
    ├─ Build on Windows (MSVC)
    ├─ Integration tests
    └─ Code quality checks
    ↓
All checks pass → Ready to merge
```

### Release Flow

```
Create Release Tag (e.g., v1.0.0)
    ↓
Conan Deploy Workflow Runs
    ├─ Build Conan packages
    │   ├─ Linux (C++17/20/23)
    │   └─ Windows (C++17/20/23)
    ├─ Test packages
    ├─ Upload to Conan remote
    └─ Create release artifacts
    ↓
Package available in Conan
Source/header archives attached to release
```

## Local Testing

Before pushing, you can test workflows locally using [act](https://github.com/nektos/act):

```bash
# Install act
brew install act  # macOS
# or
curl https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash  # Linux

# Run CI workflow
act -j linux-build

# Run specific job
act -j integration-test -P ubuntu-latest=ghcr.io/catthehacker/ubuntu:act-latest
```

## Workflow Maintenance

### Updating Dependencies

Update actions versions regularly:
```yaml
# Check for new versions at:
# - actions/checkout: https://github.com/actions/checkout/releases
# - actions/setup-python: https://github.com/actions/setup-python/releases
# - actions/upload-artifact: https://github.com/actions/upload-artifact/releases
```

### Testing Matrix Updates

When adding new platforms or C++ standards:
1. Update the matrix in `ci.yml`
2. Test locally with act if possible
3. Create a PR to validate
4. Merge after validation

## Troubleshooting

### CI Failures

**Build failures**:
- Check compiler versions in matrix
- Verify C++ standard compatibility
- Review error logs in Actions tab

**Integration test failures**:
- Ensure CMakeLists.txt is correct
- Check FetchContent/CPM configuration
- Verify paths in test scripts

**Code quality failures**:
- Run `clang-format` locally
- Check for tabs vs spaces in CMake files
- Validate markdown links

### Conan Deployment Issues

**Package creation fails**:
- Verify conanfile.py syntax
- Check version format (use semantic versioning)
- Ensure dependencies are available

**Upload fails**:
- Verify secrets are configured
- Check remote URL is correct
- Ensure credentials are valid

**Test package fails**:
- Verify package can be consumed
- Check CMakeLists.txt in test
- Review compiler settings

## Best Practices

1. **Run tests locally** before pushing
2. **Keep workflows updated** with latest action versions
3. **Monitor workflow runs** for failures
4. **Update documentation** when adding new workflows
5. **Use semantic versioning** for releases
6. **Tag releases properly** (e.g., v1.0.0, not 1.0.0)

## Status Badges

Add these to README.md:

```markdown
[![CI](https://github.com/Katze719/ThreadSchedule/workflows/CI/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/ci.yml)
[![Conan Deploy](https://github.com/Katze719/ThreadSchedule/workflows/Conan%20Deploy/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/conan-deploy.yml)
[![Documentation](https://github.com/Katze719/ThreadSchedule/workflows/Documentation/badge.svg)](https://github.com/Katze719/ThreadSchedule/actions/workflows/documentation.yml)
```

## Contributing

When adding new workflows:
1. Document in this README
2. Add status badge
3. Test with `act` if possible
4. Create PR with workflow changes
5. Monitor first runs carefully
