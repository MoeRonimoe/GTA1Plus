# Überträgt Adressen zwischen eng verwandten Builds (z.B. London 1969 -> 1961) über maskierte Bytefolgen:
# absolute Adressen und rel32-Sprungziele werden ausgeblendet, der Rest muss exakt passen.
import re, struct
from pe import PE

def text_range(pe):
    for name, va, vs, ra, rs in pe.secs:
        if name == ".text":
            return pe.base + va, ra, min(vs, rs)
    raise ValueError("kein .text")

def masked_pattern(data, lo=0x400000, hi=0x900000):
    """Regex-Muster: 4-Byte-Werte im Adressbereich und rel32 nach E8/E9 werden zu Platzhaltern"""
    out, i = [], 0
    while i < len(data):
        if data[i] in (0xE8, 0xE9) and i + 5 <= len(data):
            out.append(re.escape(bytes([data[i]])) + b"....")
            i += 5
            continue
        if i + 4 <= len(data):
            v = struct.unpack_from("<I", data, i)[0]
            if lo <= v < hi:
                out.append(b"....")
                i += 4
                continue
        out.append(re.escape(bytes([data[i]])))
        i += 1
    return re.compile(b"".join(out), re.S)

class Porter:
    def __init__(self, src_path, dst_path):
        self.s, self.d = PE(src_path), PE(dst_path)
        self.s_text = text_range(self.s)
        self.d_text = text_range(self.d)

    def s_bytes(self, va, n):
        return self.s.read(va, n)

    def d_find(self, pattern):
        va, ra, size = self.d_text
        return [va + m.start() - ra for m in pattern.finditer(self.d.d, ra, ra + size)]

    def code(self, va, before=0, length=24):
        """Codestelle va (Befehl/Funktion) übertragen; eindeutig oder None"""
        for n in (length, length * 2, length * 3):
            pat = masked_pattern(self.s_bytes(va - before, n + before))
            hits = self.d_find(pat)
            if len(hits) == 1:
                return hits[0] + before
            if not hits:
                return None
        return None

    def data(self, addr, max_refs=12):
        """Datenadresse über Befehle übertragen, die sie verwenden (Mehrheitsentscheid)"""
        va, ra, size = self.s_text
        needle = struct.pack("<I", addr)
        votes = {}
        for m in re.finditer(re.escape(needle), self.s.d[ra:ra + size]):
            ref = va + m.start()
            ctx = self.s.read(ref - 12, 28)
            pat = masked_pattern(ctx)
            hits = self.d_find(pat)
            if len(hits) == 1:
                v = struct.unpack("<I", self.d.read(hits[0] + 12, 4))[0]
                votes[v] = votes.get(v, 0) + 1
            if sum(votes.values()) >= max_refs:
                break
        if not votes:
            return None, 0
        best = max(votes, key=votes.get)
        return best, votes[best]
