import glob
import os
import sys

MAGIC_PATHS = [os.environ.get("MAGIC_MGC")] if os.environ.get("MAGIC_MGC") else []

MAGIC_PATHS += glob.glob("vcpkg_installed/*/share/libmagic/misc/magic.mgc")
MAGIC_PATHS += glob.glob("build/vcpkg_installed/*/share/libmagic/misc/magic.mgc")
MAGIC_PATHS += [
    "/vcpkg/installed/x64-linux/share/libmagic/misc/magic.mgc",
    "/work/vcpkg/installed/x64-linux/share/libmagic/misc/magic.mgc",
    "/usr/lib/file/magic.mgc"
]

data = None
for path in MAGIC_PATHS:
    try:
        with open(path, "rb") as f:
            data = f.read()
        break
    except OSError:
        continue

if data is None:
    print("magic_static.py: magic.mgc not found in any of: %s" % MAGIC_PATHS, file=sys.stderr)
    sys.exit(1)

print("char magic_database_buffer[%d] = {%s};" % (len(data), ",".join(str(int(b)) for b in data)))
