# Contributing to tsukumo-core

Thank you for helping. This page tells you how to set up the repository, check
a change and submit it. For a large change, open an issue first so that you and
the maintainer can agree on the approach before you write it. The
[Contributing section of the README](README.md#contributing) states the license
your contribution is under.

## Set up

To build the kernel, follow [Build](README.md#build) in the README. To build and
run the tests, follow [Building and running the tests](docs/developer/testing.md):

- [Run the QEMU tests](docs/developer/testing.md#run-the-qemu-tests) runs the
  tests on QEMU with CTest.
- [Run the PC unit tests](docs/developer/testing.md#run-the-pc-unit-tests)
  builds and runs the tests that need no QEMU and no RISC-V toolchain.

The repository checks formatting with [pre-commit](https://pre-commit.com),
which needs Python. Install it once per clone, in a virtual environment. On
Windows, run:

```shell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\pre-commit install
```

On Linux or macOS, replace `.venv\Scripts\` with `.venv/bin/`.

The hooks do the following:

- check whitespace, line endings, file ends, YAML, JSON and XML syntax, and
  large files;
- format C and C++ with clang-format, and CMake files with cmake-format;
- run `reuse lint`, which fails when a file has no license and copyright
  information.

After `pre-commit install`, the hooks run on every `git commit`. The first
commit downloads the hooks, so it needs network access. If a hook changes a
file, the commit stops: stage the change and commit again.

## Make a change

1. Start a branch from `main`. The branch name is up to you.
2. Keep one concern in one pull request.
3. If the change alters behavior, add a test with it. For a bug fix, add a test
   that fails before the fix. Changes to documentation or formatting need no
   test.
4. Before you open the pull request, build the tests as
   [Building and running the tests](docs/developer/testing.md) describes,
   because `ctest` runs only what is already built. Use the `host` preset for
   the PC tests and the `debug` preset for the QEMU tests.
5. From the root of the repository, run both sets of tests:
   - the PC tests: `ctest --preset host`
   - the QEMU tests:
     `ctest --test-dir ../build/debug --output-on-failure --no-tests=error`

   If CMake did not find QEMU when it configured the build, it registers no
   QEMU tests, and `--no-tests=error` makes the second command fail.
6. If the change alters the kernel, the tests or the build, add one line under
   `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md), below the heading that fits
   (Added, Changed, Fixed or Removed). A change to documentation alone needs
   none.

## License headers

Put an SPDX header at the top of every new source file, as the existing files
have. Markdown, YAML and JSON files need none: [REUSE.toml](REUSE.toml) gives
them the MIT License by extension. For any other format that cannot hold a
header, add its path to `REUSE.toml`.

To write the license list of the repository as an SPDX document, run
`uv run --with "reuse[charset-normalizer]" reuse spdx -o <file>`. Only this
command needs [uv](https://docs.astral.sh/uv/). The check in `git commit` runs
in the environment that pre-commit builds.

## Commit messages

The maintainer recommends the form `type(scope): subject`, for example
`fix(idle): halt the idle task with wfi instead of spinning`. Use the body to
say why you made the change: the problem and the reason for the approach.

## Pull requests

In the description, say what the change does and why, and which tests you ran.
The maintainer decides how a pull request is merged.
