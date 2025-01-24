
MAGIC_PATHS = [
    "/vcpkg/installed/x64-linux/share/libmagic/misc/magic.mgc",
    "/work/vcpkg/installed/x64-linux/share/libmagic/misc/magic.mgc",
    "/usr/lib/file/magic.mgc"
]

for path in MAGIC_PATHS:
    try:
        with open(path, "rb") as f:
            data = f.read()
        break
    except:
        continue

print("char magic_database_buffer[%d] = {%s};" % (len(data), ",".join(str(int(b)) for b in data)))
