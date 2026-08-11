## Layout

Test sources live **next to the code they cover**, named `*.spec.cpp`:

```
libscan/text/text.c
libscan/text/text.spec.cpp     <- tests for text.c
src/util.c
src/util.spec.cpp
```

Suites that have no single owning source file live here instead:

| Path                       | What it holds                                                          |
|----------------------------|------------------------------------------------------------------------|
| `tests/support/`           | `ScanTest`/`ArcTest` fixtures, parser context factories, corpus helpers |
| `tests/corpus/smoke.spec.cpp` | Parses every file of the test corpus with the parser its extension maps to |
| `tests/corpus/fuzz.spec.cpp`  | Feeds mangled files to the parsers                                  |

Spec files are globbed with `CONFIGURE_DEPENDS`, so a new one is picked up by a plain `make` —
no CMake edit needed.

Two sets of binaries are built, each in a plain, an ASan and a UBSan variant:

| Binaries                                     | Links against | Sources                                |
|----------------------------------------------|---------------|----------------------------------------|
| `scan_test`, `scan_a_test`, `scan_ub_test`   | `scan`        | `libscan/**/*.spec.cpp`, `tests/corpus` |
| `sist2_test`, `sist2_a_test`, `sist2_ub_test`| `sist2_core`  | `src/**/*.spec.cpp`                    |

## Build

Tests need the `tests` vcpkg manifest feature (gtest):

```bash
cmake -DBUILD_TESTS=on -DVCPKG_MANIFEST_FEATURES=tests ..
make scan_test sist2_test -j $(nproc)
```

## Run

```bash
ctest -j $(nproc)          # from build/, runs every variant, one process per test
./build/scan_test          # or a single binary directly, from any directory
```

The OCR tests need a tessdata folder containing `eng.traineddata`. It is looked up in
`$TESSDATA_PREFIX`, then `./tessdata`, then the usual distro locations.

### Run fuzz tests:
```bash
./build/scan_a_test --gtest_filter=*Fuzz* --gtest_repeat=100
```

## Writing a test

```cpp
#include "tests/support/scan_fixture.h"

class TextTest : public ScanTest {
protected:
    scan_text_ctx_t ctx = make_text_ctx();   // fresh context per test
};

TEST_F(TextTest, BookCsvContentLen) {
    load("text/books.csv");                  // path relative to the corpus root

    parse_text(&ctx, &f, &doc);

    ASSERT_NEAR(content_len(), 500, 4);
}
```

`ScanTest` owns `f` (vfile) and `doc` (document) and frees both at teardown, including when an
`ASSERT_*` aborts the test body. `ArcTest` adds `recurse_into()`, which installs a parser for the
sub-documents of an archive and keeps them in `sub_docs`.

Tests that write to disk must use `temp_path()` from `tests/support/temp_path.h`: `ctest` runs the
three variants of a binary at the same time, so fixed paths collide.

## Coverage

```bash
cmake -DBUILD_TESTS=on -DVCPKG_MANIFEST_FEATURES=tests -DSIST_COVERAGE=on ..
make scan_test sist2_test -j $(nproc)
ctest -j $(nproc)
make coverage        # needs gcovr; writes build/coverage/index.html
```
