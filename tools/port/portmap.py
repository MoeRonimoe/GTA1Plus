# Ordnet Funktionen, Codestellen und Datenadressen von R2P (GTA.exe 1.0.0.1) einer anderen Version zu
# python portmap.py <ziel.asm> -> <ziel>.map.pkl
# Listings mit dumpbin aus Visual Studio (Hostx64\x86): dumpbin /nologo /disasm:nobytes /out:r2p.asm GTA.exe
# Stand 25.09.2026: London 1969 ~260, WINO ~400 von 1898 Funktionen zugeordnet - der Spielcode ist dort
# stark umgebaut (WINO: andere Strukturgrößen, z.B. Spieler 444 statt 417 Byte), also viel Handarbeit.
# London 1961 ist zu 79 % identisch mit London 1969.
import sys, re, pickle, difflib
from collections import Counter, defaultdict
from asm import Prog, norm, HEX, is_addr

def ngrams(sig, n=3):
    return set(sig[i:i + n] for i in range(max(1, len(sig) - n + 1)))

def addrs_in(ops):
    return [int(m.group(1), 16) for m in HEX.finditer(ops) if is_addr(int(m.group(1), 16))]

def align(fa, fb):
    """Paare (Befehl aus fa, Befehl aus fb) für gleich normierte Befehle"""
    a = [norm(mn, ops) for _, mn, ops in fa]
    b = [norm(mn, ops) for _, mn, ops in fb]
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    pairs = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal" or (tag == "replace" and i2 - i1 == j2 - j1):
            for k in range(i2 - i1):
                if fa[i1 + k][1] == fb[j1 + k][1]:
                    pairs.append((fa[i1 + k], fb[j1 + k]))
    return pairs, sm.ratio()

def build(src, dst):
    ssig = {s: src.sig(s) for s in src.funcs}
    dsig = {s: dst.sig(s) for s in dst.funcs}
    sc, dc = Counter(ssig.values()), Counter(dsig.values())
    fmap = {}
    for s, g in ssig.items():
        if len(g) > 3 and sc[g] == 1 and dc.get(g) == 1:
            fmap[s] = next(d for d, h in dsig.items() if h == g)
    print("exakt:", len(fmap))

    # Ähnlichkeit über seltene 3-Gramme
    sgr = {s: ngrams(g) for s, g in ssig.items()}
    dgr = {s: ngrams(g) for s, g in dsig.items()}
    inv = defaultdict(set)
    for d, gr in dgr.items():
        for x in gr:
            inv[x].add(d)
    best = {}
    for s, gr in sgr.items():
        if s in fmap or len(ssig[s]) < 6:
            continue
        cand = Counter()
        for x in gr:
            ds = inv.get(x)
            if ds and len(ds) < 40:
                for d in ds:
                    cand[d] += 1
        scored = []
        for d, _ in cand.most_common(15):
            j = len(gr & dgr[d]) / len(gr | dgr[d])
            scored.append((j, d))
        scored.sort(reverse=True)
        if scored:
            best[s] = scored[:3]
    # sicher: deutlich besser als der Zweitbeste
    used = set(fmap.values())
    for s, sc3 in sorted(best.items(), key=lambda kv: -kv[1][0][0]):
        j, d = sc3[0]
        second = sc3[1][0] if len(sc3) > 1 else 0
        if j >= 0.45 and j - second >= 0.12 and d not in used:
            fmap[s] = d
            used.add(d)
    print("mit Ähnlichkeit:", len(fmap))

    # Aufrufe und Daten über ausgerichtete Befehle übertragen (mehrere Runden)
    for rnd in range(4):
        votes = defaultdict(Counter)
        dvotes = defaultdict(Counter)
        for s, d in list(fmap.items()):
            pairs, _ = align(src.funcs[s], dst.funcs[d])
            for (a1, m1, o1), (a2, m2, o2) in pairs:
                if m1 == "call" and re.fullmatch(r"[0-9A-F]{8}", o1) and re.fullmatch(r"[0-9A-F]{8}", o2):
                    votes[src.resolve(int(o1, 16))][dst.resolve(int(o2, 16))] += 1
                x1, x2 = addrs_in(o1), addrs_in(o2)
                if len(x1) == len(x2):
                    for p, q in zip(x1, x2):
                        dvotes[p][q] += 1
        added = 0
        for s, c in votes.items():
            d, n = c.most_common(1)[0]
            if s in src.funcs and s not in fmap and d in dst.funcs and d not in used and n >= 1 and n == sum(c.values()):
                fmap[s] = d
                used.add(d)
                added += 1
        print("Runde", rnd, "neu über Aufrufe:", added, "gesamt:", len(fmap))
        if not added:
            break
    dmap = {p: c.most_common(1)[0] for p, c in dvotes.items()}
    return fmap, dmap

if __name__ == "__main__":
    src = Prog("r2p.asm")
    dst = Prog(sys.argv[1])
    fmap, dmap = build(src, dst)
    pickle.dump((fmap, dmap), open(sys.argv[1].replace(".asm", ".map.pkl"), "wb"))
