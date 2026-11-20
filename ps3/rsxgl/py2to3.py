# py2to3.py <file>...: the Python 2 forms in the 2012 Mesa generator scripts RSXGL's build runs (GLSL built-ins,
# gallium's format tables), rewritten for Python 3 in place: print statements (a trailing comma: end=' '), xrange,
# iteritems, StringIO, subprocess output as bytes, and the integer divisions those scripts rely on.
import re
import sys

for fn in sys.argv[1:]:
    src = open(fn).read().split('\n')
    out = []
    in_triple = False
    for line in src:
        if in_triple:
            if line.rstrip().endswith(in_triple):
                line = line.rstrip() + ')'
                in_triple = False
            out.append(line)
            continue
        m = re.match(r'^(\s*)print(\s+(.*))?$', line)
        if m and not line.strip().startswith('print('):
            ind, rest = m.group(1), (m.group(3) or '')
            if rest.startswith('(') and rest.count('(') > rest.count(')'):
                out.append(line)  # print (a +  newline  b): already a call in Python 3
                continue
            q = next((t for t in ('r"""', '"""', "r'''", "'''") if rest.startswith(t)), None)
            if q and rest.count(q.lstrip('r')) == 1:
                out.append(ind + 'print(' + rest)
                in_triple = q.lstrip('r')
                continue
            comment = ''
            hash_at = rest.find('#')
            if hash_at > 0 and rest[:hash_at].rstrip().endswith(','):  # print x,   # note
                comment = '  ' + rest[hash_at:]
                rest = rest[:hash_at].rstrip()
            if rest.endswith(','):
                line = ind + 'print(' + rest[:-1] + ", end=' ')" + comment
            else:
                line = ind + 'print(' + rest + ')' + comment
        out.append(line)
    s = '\n'.join(out)
    s = re.sub(r'\bxrange\(', 'range(', s)
    s = s.replace('.iteritems()', '.items()').replace('import StringIO', 'import io as StringIO')
    s = s.replace('output = p.communicate()[0]', 'output = p.communicate()[0].decode()')
    s = s.replace("re.sub('\\s\\s+'", "re.sub(r'\\s\\s+'")
    # integer division where Python 2's / was one (sizes in bits and bytes)
    for a, b in (('self.size/2', 'self.size//2'), ('self.block_size()/8', 'self.block_size()//8'),
                 ('type.size/2', 'type.size//2'), ('type.size / 2', 'type.size // 2'),
                 ('format.block_size() / 8,', 'format.block_size() // 8,')):
        s = s.replace(a, b)
    open(fn, 'w').write(s)
