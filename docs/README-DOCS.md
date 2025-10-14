### API Documentation (Doxygen + Awesome Theme)

This project provides a modern Doxygen setup using the Doxygen Awesome theme.

Prerequisites:
- Doxygen installed (via your package manager)

Build the documentation:
```bash
cmake -S . -B build -DTHREADSCHEDULE_BUILD_DOCS=ON
cmake --build build --target docs
```

Open the generated HTML:
- `build/docs/doxygen/html/index.html`

Notes:
- The Awesome theme assets are automatically downloaded into the build directory during configure.
- The documentation includes headers under `include/` and uses the repository `README.md` as the landing page.

Additional guides:
- [Integration Guide](INTEGRATION.md)
- [Thread Registry](REGISTRY.md)
- [Scheduled Tasks](SCHEDULED_TASKS.md)
- [Error Handling](ERROR_HANDLING.md)
- [CMake Reference](CMAKE_REFERENCE.md)
- [Profiles](PROFILES.md)
- [Topology & NUMA](TOPOLOGY_NUMA.md)
- [Chaos Testing](CHAOS_TESTING.md)

