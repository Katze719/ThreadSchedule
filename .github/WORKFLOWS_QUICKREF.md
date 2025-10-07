# GitHub Actions Quick Reference

## Workflows at a Glance

| Workflow | Trigger | Purpose | Duration |
|----------|---------|---------|----------|
| **CI** | Push, PR | Build & test all configurations | ~15-30 min |
| **Conan Deploy** | Release, Manual | Package & deploy to Conan | ~20-40 min |
| **Documentation** | Push, PR | Validate docs & links | ~2-5 min |

## Quick Commands

### View Workflow Runs
```bash
# Go to repository page
https://github.com/Katze719/ThreadSchedule/actions

# Or use GitHub CLI
gh run list
gh run view <run-id>
```

### Trigger Manual Workflows

**Conan Deploy:**
```bash
# Via GitHub UI
Actions → Conan Deploy → Run workflow
- Version: 1.0.0
- Channel: stable

# Via GitHub CLI
gh workflow run conan-deploy.yml -f version=1.0.0 -f channel=stable
```

## Workflow Status Checks

### Required for PR Merge (recommended)
- ✅ Linux (ubuntu-latest, C++17, gcc)
- ✅ Windows (C++17, msvc)
- ✅ Integration Test (subdirectory)
- ✅ Code Quality

### Optional but Useful
- Multiple C++ standards
- Documentation checks

## Secrets Configuration

### For Conan Deployment

Navigate to: `Settings → Secrets and variables → Actions → New repository secret`

| Secret Name | Description | Required |
|-------------|-------------|----------|
| `CONAN_REMOTE_URL` | Your Conan server URL | Yes* |
| `CONAN_LOGIN_USERNAME` | Conan username | Yes* |
| `CONAN_PASSWORD` | Conan password | Yes* |
| `CONAN_USERNAME` | Package namespace | No |

\* Only required if using custom Conan remote. ConanCenter uses manual PR process.

## Common Issues & Solutions

### CI Failures

**Issue**: Build fails on specific platform
```bash
# Check logs
Actions → Failed workflow → Failed job → View logs

# Test locally with act (Linux only)
act -j linux-build -P ubuntu-latest=ghcr.io/catthehacker/ubuntu:act-latest
```

**Issue**: Integration test fails
```bash
# Verify CMakeLists.txt syntax
cmake -B build -S . --debug-output

# Check integration method locally
mkdir test_integration
cd test_integration
# ... follow integration test steps
```

### Conan Deployment Issues

**Issue**: Package creation fails
```bash
# Test locally
conan create . --version=1.0.0 --build=missing

# Validate conanfile.py
python3 -c "from conanfile import ThreadScheduleConan; print('Valid')"
```

**Issue**: Upload fails
```bash
# Verify secrets are set
gh secret list

# Test upload manually
conan upload "threadschedule/1.0.0@*" --remote=your_remote --confirm
```

### Documentation Failures

**Issue**: Broken links
```bash
# Check links locally
npm install -g markdown-link-check
markdown-link-check README.md
```

**Issue**: Missing examples
```bash
# Verify examples in docs
grep -r "cmake_minimum_required" *.md
grep -r "#include <threadschedule" *.md
```

## Performance Tips

### Speed Up CI

1. **Cache dependencies**: Already configured for Conan
2. **Parallel builds**: Using `-j` flag
3. **Fail fast**: Set to `false` to see all failures
4. **Matrix optimization**: Remove redundant configurations

### Reduce Action Minutes

1. **Skip CI**: Add `[skip ci]` to commit message
   ```bash
   git commit -m "Update docs [skip ci]"
   ```

2. **Use manual triggers**: For expensive workflows

## Maintenance

### Update Dependencies

Check for updates quarterly:
```yaml
# actions/checkout
uses: actions/checkout@v4  # Check: github.com/actions/checkout/releases

# actions/setup-python
uses: actions/setup-python@v5  # Check: github.com/actions/setup-python/releases

# actions/upload-artifact
uses: actions/upload-artifact@v4  # Check: github.com/actions/upload-artifact/releases
```

### Monitor Usage

```bash
# View workflow usage (requires admin)
Settings → Billing → Usage

# GitHub Actions minutes:
# - Public repos: Unlimited
# - Private repos: 2000 min/month (free tier)
```

## Best Practices

1. ✅ **Always test locally first** before pushing
2. ✅ **Use draft PRs** for WIP changes
3. ✅ **Monitor workflow runs** for failures
4. ✅ **Keep workflows updated** with latest actions
5. ✅ **Document changes** in workflow files
6. ✅ **Use caching** for dependencies
7. ✅ **Set up branch protection** for main branch

## Useful Links

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Workflow Syntax](https://docs.github.com/en/actions/reference/workflow-syntax-for-github-actions)
- [Action Marketplace](https://github.com/marketplace?type=actions)
- [Act - Local Testing](https://github.com/nektos/act)
- [Conan Documentation](https://docs.conan.io)

## Support

For issues with workflows:
1. Check workflow logs
2. Review `.github/workflows/README.md`
3. Open issue with logs attached
4. Tag maintainers if urgent

---

**Last Updated**: When workflows were added to repository
**Maintainer**: @Katze719
