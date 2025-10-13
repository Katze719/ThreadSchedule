# ThreadSchedule Documentation

This directory contains the source files for the ThreadSchedule documentation, built with [MkDocs](https://www.mkdocs.org/) and the [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) theme.

## Viewing the Documentation

The documentation is automatically built and deployed to GitHub Pages on every push to the main branch:

🌐 **[https://katze719.github.io/ThreadSchedule/](https://katze719.github.io/ThreadSchedule/)**

## Building Locally

### Prerequisites

- Python 3.8 or newer
- pip (Python package installer)

### Installation

Install the required Python packages:

```bash
pip install -r requirements.txt
```

Or install system packages (Ubuntu/Debian):

```bash
sudo apt-get install python3-pip doxygen graphviz
pip3 install mkdocs mkdocs-material mkdoxy
```

### Building the Documentation

Build the documentation site:

```bash
mkdocs build
```

The generated site will be in the `site/` directory.

### Previewing Locally

Start a local development server with live reload:

```bash
mkdocs serve
```

Then open your browser to [http://localhost:8000](http://localhost:8000)

The server will automatically reload when you make changes to the documentation source files.

## Documentation Structure

```
docs/
├── index.md                      # Home page
├── getting-started/              # Getting started guides
│   ├── installation.md           # Installation instructions
│   └── quick-start.md            # Quick start guide
├── user-guide/                   # User guides
│   ├── thread-wrappers.md        # Thread wrapper documentation
│   └── thread-pools.md           # Thread pool documentation
├── development/                  # Development documentation
│   ├── building.md               # Building from source
│   ├── contributing.md           # Contributing guide
│   └── performance.md            # Performance guide
├── CMAKE_REFERENCE.md            # CMake reference
├── ERROR_HANDLING.md             # Error handling guide
├── INTEGRATION.md                # Integration guide
├── REGISTRY.md                   # Thread registry guide
└── SCHEDULED_TASKS.md            # Scheduled tasks guide
```

## API Documentation

The API documentation is automatically generated from C++ source code using [Doxygen](https://www.doxygen.nl/) and integrated into MkDocs using the [mkdoxy](https://github.com/JakubAndrysek/mkdoxy) plugin.

The API documentation is generated during the `mkdocs build` process and appears under the `/ThreadSchedule/` path in the built site.

## Contributing to Documentation

### Adding New Pages

1. Create a new Markdown file in the appropriate directory
2. Add the page to the navigation in `mkdocs.yml`
3. Build and preview locally to verify
4. Submit a pull request

### Documentation Style Guide

- Use clear, concise language
- Include code examples for features
- Add cross-references to related documentation
- Use proper Markdown formatting
- Test all code examples

### Markdown Extensions

The documentation uses these Markdown extensions:

- **Code highlighting**: `pymdownx.highlight`
- **Inline code**: `pymdownx.inlinehilite`
- **Code snippets**: `pymdownx.snippets`
- **Admonitions**: Warning, note, tip boxes
- **Tabbed content**: Multiple code examples in tabs
- **Table of contents**: Automatic ToC generation

Example admonition:

```markdown
!!! note "Note Title"
    This is a note with additional information.

!!! warning
    This is a warning message.

!!! tip
    This is a helpful tip.
```

## Deployment

Documentation is automatically deployed to GitHub Pages via GitHub Actions when changes are pushed to the `main` branch.

The deployment workflow is defined in `.github/workflows/mkdocs-deploy.yml`.

### Manual Deployment

If needed, you can manually deploy using:

```bash
mkdocs gh-deploy
```

This will build the documentation and push it to the `gh-pages` branch.

## Troubleshooting

### Build Errors

If you encounter build errors:

1. Make sure all dependencies are installed: `pip install -r requirements.txt`
2. Ensure Doxygen is installed: `doxygen --version`
3. Check for broken links: `mkdocs build --strict`

### Missing API Documentation

If API documentation is not generated:

1. Verify Doxygen is installed
2. Check that source files exist in `include/threadschedule/`
3. Review mkdoxy configuration in `mkdocs.yml`

## Resources

- [MkDocs Documentation](https://www.mkdocs.org/)
- [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
- [mkdoxy Plugin](https://github.com/JakubAndrysek/mkdoxy)
- [Doxygen Documentation](https://www.doxygen.nl/manual/)
