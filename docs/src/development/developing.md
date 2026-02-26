# Developing Sipi

## Using an IDE

### CLion

If you are using the [CLion](https://www.jetbrains.com/clion/) IDE, put
`-j 1` in Preferences -&gt; Build, Execution, Deployment -&gt; CMake
-&gt; Build options, to prevent CMake from building with multiple
processes. Also, note that code introspection in the CLion editor may
not work until it has run CMake.

### Code::Blocks

If you are using the Code::Blocks\_ IDE, you can build a cdb project:

    cd build
    cmake .. -G "CodeBlocks - Unix Makefiles"

## Writing Tests

We use two test frameworks. We use
[googletest](https://github.com/google/googletest) for unit test and
[pytest](http://doc.pytest.org/en/latest/) for end-to-end tests.

### Unit Tests

TBA

### End-to-End Tests

To add end-to-end tests, add a Python class in a file whose name begins
with `test`, in the `test` directory. The class's methods, whose names
must also begin with `test`, should use the `manager` fixture defined in
`test/conftest.py`, which handles starting and stopping a Sipi server,
and provides other functionality useful in tests. See the existing
`test/test_*.py` files for examples.

To facilitate testing client HTTP connections in Lua scripts, the
`manager` fixture also starts and stops an `nginx` instance, which can
be used to simulate an authorization server. For example, the provided
`nginx` configuration file, `test/nginx/nginx.conf`, allows `nginx` to
act as a dummy [Knora](http://www.knora.org/) API server for permission
checking: its `/v1/files` route returns a static JSON file that always
grants permission to view the requested file.

## Commit Message Schema

When writing commit messages, we stick to this schema:

    type (scope): subject
    body

Types:

-   feature (new feature for the user)
-   fix (bug fix for the user)
-   docs (changes to the documentation)
-   style (formatting, etc)
-   refactor (refactoring production code, e.g. renaming a variable)
-   test (adding missing tests, refactoring tests)
-   build (changes to CMake configuration, etc.)
-   enhancement (residual category)

Example:

    feature (HTTP server): support more authentication methods
