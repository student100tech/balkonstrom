import sys, struct, re, glob
def uleb(d, p):
    r = s = 0
    while True:
        b = d[p]; p += 1; r |= (b & 0x7f) << s
        if b < 0x80: return r, p
        s += 7
out = set()
for f in glob.glob(sys.argv[1] + '/**/*.dex', recursive=True):
    d = open(f, 'rb').read()
    n, off = struct.unpack('<II', d[56:64])
    for i in range(n):
        so = struct.unpack('<I', d[off + 4*i: off + 4*i + 4])[0]
        _, p = uleb(d, so); e = d.index(b'\0', p)
        try: s = d[p:e].decode('utf-8')
        except: continue
        out.add(s)
pat = re.compile(r'^/?[a-z][A-Za-z0-9_-]*(/[A-Za-z0-9_{}.-]+){1,6}/?$')
for s in sorted(out):
    if pat.match(s) and not s.startswith(('L', 'android', 'java', 'kotlin', 'com/', 'org/', 'res/', 'META-INF')) and not s.endswith(('.java', '.kt', '.png', '.xml')):
        print(s)
print('== mit zendure/zen-iot/http', file=sys.stderr)
for s in sorted(out):
    if ('zendure' in s.lower() or 'zen-iot' in s) and len(s) < 200: print('URL ' + s)
