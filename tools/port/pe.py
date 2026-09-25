# Hilfen für PE-Dateien: Importe mit IAT-Adressen, Strings und Verweise darauf
import struct, re

class PE:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3c)[0]
        n = struct.unpack_from("<H", d, pe + 6)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.timestamp = struct.unpack_from("<I", d, pe + 8)[0]
        self.dd = pe + 24 + 96
        self.secs = []
        for i in range(n):
            o = pe + 24 + opt + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode("latin1")
            vs, va, rs, ra = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((name, va, vs, ra, rs))

    def off(self, rva):
        for _, va, vs, ra, rs in self.secs:
            if va <= rva < va + max(vs, rs) and rva - va < rs:
                return ra + rva - va
        return None

    def va_of_off(self, o):
        for _, va, vs, ra, rs in self.secs:
            if ra <= o < ra + rs:
                return self.base + va + o - ra
        return None

    def read(self, va, n):
        o = self.off(va - self.base)
        return self.d[o:o + n] if o is not None else None

    def cstr(self, o):
        return self.d[o:self.d.index(b"\0", o)].decode("latin1")

    def imports(self):
        """{(dll, name): IAT-Adresse}"""
        res = {}
        irva = struct.unpack_from("<I", self.d, self.dd + 8)[0]
        o = self.off(irva)
        while o is not None:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.d, o)
            if not name_rva:
                break
            dll = self.cstr(self.off(name_rva))
            thunk = oft or ft
            k = 0
            while True:
                t = struct.unpack_from("<I", self.d, self.off(thunk) + 4 * k)[0]
                if not t:
                    break
                if not t & 0x80000000:
                    fn = self.cstr(self.off(t) + 2)
                    res[(dll.upper(), fn)] = self.base + ft + 4 * k
                k += 1
            o += 20
        return res

    def find_strings(self, pattern):
        """[(va, text)] für ASCII-Strings, die zum regulären Ausdruck passen"""
        out = []
        for m in re.finditer(rb"[\x20-\x7e]{3,}\x00", self.d):
            s = m.group()[:-1].decode("latin1")
            if re.search(pattern, s):
                out.append((self.va_of_off(m.start()), s))
        return out
