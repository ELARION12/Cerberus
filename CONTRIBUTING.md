# Contributing

Bug reports, fixes and benchmark results from other hardware are all welcome.

## Build and test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
PYTHONPATH=python pytest tests/python_tests
```

`pip install .` builds the same code into a wheel. CI does both on x86-64 and ARM64, plus a Debug build with AddressSanitizer for the C and C++ tests. You can run that one yourself with `-DCMAKE_BUILD_TYPE=Debug`.

## Before you open a pull request

- Add a test for what you changed. The C and C++ tests use `assert()`, and the test targets keep it on in every build type, so do not work around that.
- Do not add `-march=native` to the default build. Machine-specific kernels go behind the run-time check in `src/cpp_engine/core/Simd.hpp`, and each one needs a test that compares it with the scalar path (see `tests/python_tests/test_simd.py`).
- Keep the library quiet. Progress messages use `CA_LOG` in C and C++ or `geryon._log.log` in Python, so they only appear with `GERYON_VERBOSE=1`. Errors go to stderr.
- If you touch a hot path, run `python benchmarks/context_loop.py` or `python benchmarks/vector_query.py` before and after and put the numbers in the pull request.
- Add a line under "Unreleased" in `CHANGELOG.md` for anything a user would notice.
- Keep commit messages short: a subject line, and a sentence or two only if the reason is not obvious.

## Reporting a bug

Please include your OS, CPU architecture, Python version, the output of `python -c "import geryon as c; print(c.__version__, c.simd_backend())"`, and the smallest script that shows the problem. If you suspect a vector-kernel issue, say whether it still happens with `GERYON_SIMD=scalar`.
