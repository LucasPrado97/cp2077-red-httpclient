#!/usr/bin/env python3
"""Which RED4ext address hashes does a built Mac/iOS plugin dylib use, and does each have a Mac address?

  hashscan.py <dylib> <RED4ext.SDK-macos checkout> <build dir>

The SDK reaches a game function through UniversalRelocFunc(hash): the hash is a 32-bit constant the compiler materialises with
`mov w, #lo16` + `movk w, #hi16, lsl #16` (or the high half first, or keeps as a data word). This lists every hash of the SDK's Detail/AddressHashes.hpp that
appears in the dylib that way, and where it resolves: the SDK's special cases (the vault, the vtable slots), the Mac table of the
generated <build dir>/sdk-overlay/RED4ext/Relocation-inl.hpp (the SDK table plus macos/MacSdkRows.inc), or nowhere (the trap of
macos/MacTrap.cpp: a function logs once and returns 0, a data cell reads as zeros). The functions left on the trap are the same
seven Codeware and Mod Settings leave there (their macos/NOT_YET.md): the five CBaseRTTIType_sub_80..A0 virtuals, never called, and
the two allocator error paths. The table constants themselves also show up as "data".
Read-only; nothing is executed.
"""
import re, struct, sys

def main():
    dylib, sdk, build = sys.argv[1:4]
    names = {}
    for line in open(f"{sdk}/include/RED4ext/Detail/AddressHashes.hpp", encoding="utf-8"):
        m = re.match(r"\s*constexpr\s+std::uint32_t\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if m:
            names.setdefault(int(m.group(2), 0), m.group(1))
    reloc = open(f"{build}/sdk-overlay/RED4ext/Relocation-inl.hpp", encoding="utf-8").read()
    table = {int(a, 0): int(b, 16) for a, b in re.findall(r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*0x([0-9A-Fa-f]+)\s*\}", reloc)}
    special = {int(h, 0) for h in re.findall(r"if \(aHash == (0x[0-9A-Fa-f]+|\d+)\)", reloc)}
    special |= {int(h, 0) for h in re.findall(r"aHash == (0x[0-9A-Fa-f]+|\d+)", reloc)}

    # Hashes that are data cells and have no Mac address read as a zeroed buffer (kDataHashes in MacTrap.cpp).
    trap = open(__file__.rsplit("/", 1)[0] + "/MacTrap.cpp", encoding="utf-8").read()
    zero_data = {int(v) for v in re.findall(r"(\d+)u", trap[trap.index("kDataHashes[]"):trap.index("kDataSlots")])}

    data = open(dylib, "rb").read()
    magic, _, _, _, ncmds = struct.unpack_from("<IIIII", data, 0)
    assert magic == 0xFEEDFACF, "a thin arm64 Mach-O is expected"
    off = 32
    text = []; consts = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            for i in range(nsects):
                s = off + 72 + i * 80
                sect = data[s:s + 16].rstrip(b"\0").decode(); seg = data[s + 16:s + 32].rstrip(b"\0").decode()
                ssize, soff = struct.unpack_from("<QI", data, s + 40)
                if sect == "__text":
                    text.append((soff, ssize))
                elif seg in ("__TEXT", "__DATA", "__DATA_CONST") and sect in ("__const", "__literal4", "__literal8", "__data"):
                    consts.append((soff, ssize))
        off += size
    found = {}
    for soff, ssize in text:
        words = struct.unpack_from(f"<{ssize // 4}I", data, soff)
        for i in range(len(words) - 1):
            a, b = words[i], words[i + 1]
            # movz w, #imm16 (0x52800000) then movk w, #imm16, lsl #16 (0x72A00000), same register
            value = None
            if (a & 0xFFE00000) == 0x52800000 and (b & 0xFFE00000) == 0x72A00000 and (a & 31) == (b & 31):
                value = ((a >> 5) & 0xFFFF) | (((b >> 5) & 0xFFFF) << 16)
            # or the high half first: movz w, #imm16, lsl #16 (0x52A00000) then movk w, #imm16 (0x72800000)
            elif (a & 0xFFE00000) == 0x52A00000 and (b & 0xFFE00000) == 0x72800000 and (a & 31) == (b & 31):
                value = (((a >> 5) & 0xFFFF) << 16) | ((b >> 5) & 0xFFFF)
            if value in names:
                found.setdefault(value, set()).add("code")
    for soff, ssize in consts:
        for i in range(0, ssize - 3, 4):
            value = struct.unpack_from("<I", data, soff + i)[0]
            if value in names and value > 0xFFFF:
                found.setdefault(value, set()).add("data")
    missing = 0
    for value in sorted(found, key=lambda v: names[v]):
        if value in table:
            where = f"0x{table[value]:X}"
        elif value in special:
            where = "SDK stand-in"
        elif value in zero_data:
            where = "no Mac address: data, reads as zeros"
        else:
            where = "TRAP (no Mac address: logs once, returns 0)"
            missing += 1
        print(f"{value:>10}  {names[value]:<48} {'+'.join(sorted(found[value])):<10} {where}")
    print(f"{len(found)} hashes, {missing} functions on the trap")
    return 0

if __name__ == "__main__":
    sys.exit(main())
