## Build

Tests need the `tests` vcpkg manifest feature (gtest):

```bash
cmake -DBUILD_TESTS=on -DVCPKG_MANIFEST_FEATURES=tests ..
make scan_test scan_a_test scan_ub_test -j $(nproc)
```

## Run

Always run from the repo root — test file paths are relative to it:

```bash
./build/scan_test
```

The OCR tests need a tessdata folder containing `eng.traineddata`. It is looked up in
`$TESSDATA_PREFIX`, then `./tessdata`, then the usual distro locations.

### Run fuzz tests:
```bash
./build/scan_a_test --gtest_filter=*Fuzz* --gtest_repeat=100
```
