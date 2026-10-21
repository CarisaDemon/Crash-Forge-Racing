"""Read-only PE32 reference checks for the v2.1.3 source release."""
import hashlib
import struct

def digest(data):
    return hashlib.sha256(data).hexdigest()

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]

def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]

def parse(data):
    require(len(data) >= 64 and data[:2] == b"MZ", "Missing MZ header")
    pe = u32(data, 0x3c)
    require(pe + 248 <= len(data) and data[pe:pe+4] == b"PE\0\0", "Missing PE header")
    require(u16(data, pe+4) == 0x14c and u16(data, pe+24) == 0x10b, "Expected i386 PE32")
    optional = pe+24
    count = u16(data, pe+6)
    table = optional + u16(data, pe+20)
    require(table + 40*count <= len(data), "Section table is out of bounds")
    syms, symcount = u32(data, pe+12), u32(data, pe+16)
    strings = syms + symcount*18 if syms else 0
    sections = {}
    for i in range(count):
        pos = table+40*i
        name = data[pos:pos+8].split(b"\0")[0].decode("ascii")
        if name.startswith("/") and strings:
            start = strings+int(name[1:])
            require(start < len(data), "Section name is out of bounds")
            name = data[start:data.index(b"\0", start)].decode("ascii")
        size, rva, rawsize, offset = struct.unpack_from("<IIII", data, pos+8)
        require(not rawsize or offset+rawsize <= len(data), "Section is out of bounds: "+name)
        require(name not in sections, "Duplicate PE section: "+name)
        sections[name] = dict(rva=rva, virtual_size=size, raw_size=rawsize,
                              characteristics=u32(data, pos+36), offset=offset,
                              data=data[offset:offset+rawsize] if rawsize else b"")
    header = {name:read(data, optional+off) for name, off, read in [
        ("EntryPoint",16,u32),("BaseOfCode",20,u32),("BaseOfData",24,u32),
        ("ImageBase",28,u32),("SectionAlignment",32,u32),("FileAlignment",36,u32),
        ("Subsystem",68,u16),("DllCharacteristics",70,u16),
        ("SizeOfStackReserve",72,u32),("SizeOfStackCommit",76,u32),
        ("SizeOfHeapReserve",80,u32),("SizeOfHeapCommit",84,u32)]}
    header.update(Machine=u16(data,pe+4), Characteristics=u16(data,pe+22))
    dirs = {str(i):list(struct.unpack_from("<II",data,optional+96+i*8))
            for i in range(min(16,u32(data,optional+92))) if i != 6}
    return dict(sections=sections, header=header, directories=dirs,
                coff_timestamp=u32(data,pe+8), pe_checksum=u32(data,optional+64),
                size_of_image=u32(data,optional+56))

def snapshot(data):
    parsed = parse(data)
    result = dict(header=parsed["header"], directories=parsed["directories"], sections={})
    for name, section in sorted(parsed["sections"].items()):
        if name.startswith(".debug") or name in (".gnu_debuglink", ".gnu_debugaltlink"):
            continue
        raw = section["data"]
        if name == ".edata":
            export_rva, export_size = parsed["directories"]["0"]
            field = export_rva-section["rva"]+4
            require(export_size >= 40 and field == 4 and len(raw) >= 40,
                    "Unexpected IMAGE_EXPORT_DIRECTORY layout")
            stamp = u32(raw, field)
            require(stamp in (1792552616,1792541673),
                    "Unrecognized export timestamp: "+str(stamp))
            raw = raw[:field]+bytes(4)+raw[field+4:]
        result["sections"][name] = {k:section[k] for k in
            ("rva","virtual_size","raw_size","characteristics")}
        result["sections"][name]["sha256"] = digest(raw)
    return result

def verify(data, reference):
    actual = snapshot(data)
    if actual != reference:
        differing = [name for name in sorted(set(actual["sections"]) |
                     set(reference["sections"]))
                     if actual["sections"].get(name) != reference["sections"].get(name)]
        raise RuntimeError("PE reference mismatch; sections="+repr(differing)+
            "; headers_equal="+str(actual["header"] == reference["header"])+
            "; directories_equal="+str(actual["directories"] == reference["directories"]))
    return actual
