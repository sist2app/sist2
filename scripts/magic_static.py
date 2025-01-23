
with open("/usr/lib/file/magic.mgc", "rb") as f:
    data = f.read()

print("char magic_database_buffer[%d] = {%s};" % (len(data), ",".join(str(int(b)) for b in data)))
