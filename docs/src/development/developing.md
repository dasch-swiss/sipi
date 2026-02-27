# Developing SIPI

## Using an IDE

### CLion

If you are using the [CLion](https://www.jetbrains.com/clion/) IDE, note
that code introspection in the CLion editor may not work until it has
run CMake. Open the project root directory (which contains `CMakeLists.txt`)
and let CLion configure the project automatically.

For Nix-based development, launch CLion from inside the Nix shell so it
inherits all required environment variables and dependencies:

```bash
nix develop
clion .
```

## Writing Tests

We use two test frameworks:
[GoogleTest](https://github.com/google/googletest) for unit tests and
[pytest](http://doc.pytest.org/en/latest/) for end-to-end tests.

### Unit Tests

Unit tests live in `test/unit/` and use GoogleTest with ApprovalTests.
Tests are organized by component:

- `test/unit/configuration/` - Configuration parsing tests
- `test/unit/iiifparser/` - IIIF URL parser tests
- `test/unit/sipiimage/` - Image processing tests
- `test/unit/logger/` - Logger tests
- `test/unit/handlers/` - HTTP handler tests

Run all unit tests:

```bash
make nix-test
```

Run a specific test binary directly:

```bash
cd build && test/unit/iiifparser/iiifparser
```

### End-to-End Tests

End-to-end tests live in `test/e2e/` and use pytest. To add tests,
create a Python file whose name begins with `test_` in the `test/e2e/`
directory. The test fixtures in `test/e2e/conftest.py` handle starting
and stopping a SIPI server and provide other testing utilities.

Run e2e tests:

```bash
make nix-test-e2e
```

### Smoke Tests

Smoke tests live in `test/smoke/` and run against a Docker image.
They verify basic server functionality after a Docker build:

```bash
make test-smoke
```

### Approval Tests

Approval tests live in `test/approval/` and use snapshot-based
testing for regression detection.

## Commit Message Schema

We use [Conventional Commits](https://www.conventionalcommits.org/):

    type(scope): subject
    body

Types:

- `feat` - new feature (SemVer minor)
- `fix` - bug fix (SemVer patch)
- `docs` - documentation changes
- `style` - formatting, no code change
- `refactor` - refactoring production code
- `test` - adding or refactoring tests
- `build` - changes to build system or dependencies
- `chore` - miscellaneous maintenance
- `ci` - continuous integration changes
- `perf` - performance improvements

Breaking changes are indicated with `!`:

    feat!: remove deprecated API endpoint

Example:

    feat(HTTP server): support more authentication methods
