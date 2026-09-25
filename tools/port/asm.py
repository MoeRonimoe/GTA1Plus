# Liest dumpbin-Listings (/disasm:nobytes) und zerlegt sie in Funktionen
import re, pickle, os, bisect

LINE = re.compile(r"^  ([0-9A-F]{8}): (\S+)(?:\s+(.*))?$")
HEX = re.compile(r"\b([0-9A-F]+)h\b")

def load(path):
    cache = path + ".pkl"
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(path):
        return pickle.load(open(cache, "rb"))
    ins = []
    for line in open(path, encoding="latin1"):
        m = LINE.match(line.rstrip("\n"))
        if m:
            ins.append((int(m.group(1), 16), m.group(2), (m.group(3) or "").strip()))
    pickle.dump(ins, open(cache, "wb"))
    return ins

def is_addr(v, lo=0x400000, hi=0x800000):
    return lo <= v < hi

def norm(mn, ops):
    """Operanden ohne absolute Adressen und Sprungziele"""
    def rep(m):
        v = int(m.group(1), 16)
        return "A" if is_addr(v) else m.group(0)
    if mn.startswith("j") or mn == "call":
        if re.fullmatch(r"[0-9A-F]{8}", ops):
            return mn + " L"
    return mn + " " + HEX.sub(rep, ops)

class Prog:
    def __init__(self, path):
        self.ins = load(path)
        self.addrs = [i[0] for i in self.ins]
        self.index = {a: k for k, a in enumerate(self.addrs)}
        # Inkrementell gelinkte Builds (London) rufen über Sprung-Thunks "jmp Funktion" auf
        self.thunks = {}
        for k, (a, mn, ops) in enumerate(self.ins):
            if mn == "jmp" and re.fullmatch(r"[0-9A-F]{8}", ops) and a < 0x410000 and k > 0 and \
               self.ins[k - 1][1] in ("jmp", "int") and (k + 1 == len(self.ins) or self.ins[k + 1][1] in ("jmp", "int")):
                self.thunks[a] = int(ops, 16)
        starts = set()
        for a, mn, ops in self.ins:
            if mn == "call" and re.fullmatch(r"[0-9A-F]{8}", ops):
                starts.add(self.resolve(int(ops, 16)))
        self.starts = sorted(s for s in starts if s in self.index)
        self.funcs = {}
        for n, s in enumerate(self.starts):
            e = self.starts[n + 1] if n + 1 < len(self.starts) else self.addrs[-1] + 1
            k0 = self.index[s]
            k1 = bisect.bisect_left(self.addrs, e)
            body = [i for i in self.ins[k0:k1] if i[1] != "int"]
            self.funcs[s] = body

    def resolve(self, addr):
        return self.thunks.get(addr, addr)

    def func_of(self, addr):
        n = bisect.bisect_right(self.starts, addr) - 1
        return self.starts[n] if n >= 0 else None

    def sig(self, start):
        return tuple(norm(mn, ops) for _, mn, ops in self.funcs[start])
