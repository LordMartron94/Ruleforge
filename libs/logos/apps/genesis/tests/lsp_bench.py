#!/usr/bin/env python3
"""Times the requests of `genesis lsp` on the grammars of the repository: open, change, definition, references, symbols.

  lsp_bench.py PATH/TO/genesis libs/logos
"""
import sys,os,json,subprocess,time
genesis=sys.argv[1]
root=sys.argv[2]  # libs/logos
def frame(o):
    b=json.dumps(o).encode(); return b"Content-Length: %d\r\n\r\n"%len(b)+b
def read(p):
    h=b''
    while not h.endswith(b'\r\n\r\n'): h+=p.stdout.read(1)
    n=int(h.split(b':')[1].split(b'\r\n')[0]); return json.loads(p.stdout.read(n))
p=subprocess.Popen([genesis,'lsp','-I',root+'/stdlib','-I',root+'/genesis'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
def call(m,wait_diag=False,id_=None):
    t=time.perf_counter(); p.stdin.write(frame(m)); p.stdin.flush()
    while True:
        r=read(p)
        if id_ is not None and r.get('id')==id_: break
        if wait_diag and r.get('method')=='textDocument/publishDiagnostics': break
    return (time.perf_counter()-t)*1000
call({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":'file://'+root}},id_=1)
for name in ('stdlib/XML.gen','stdlib/General.gen','genesis/Calculator.gen','stdlib/TOML.gen','stdlib/HTML.gen'):
    path=root+'/'+name; text=open(path).read(); uri='file://'+path
    t_open=call({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":uri,"version":1,"text":text}}},wait_diag=True)
    t_change=call({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":uri,"version":2},"contentChanges":[{"text":text+"\n"}]}},wait_diag=True)
    i=text.rindex('#')+1
    l=text[:i].count('\n'); c=i-(text.rfind('\n',0,i)+1)
    t_def=call({"jsonrpc":"2.0","id":10,"method":"textDocument/definition","params":{"textDocument":{"uri":uri},"position":{"line":l,"character":c+1}}},id_=10)
    t_ref=call({"jsonrpc":"2.0","id":11,"method":"textDocument/references","params":{"textDocument":{"uri":uri},"position":{"line":l,"character":c+1}}},id_=11)
    t_sym=call({"jsonrpc":"2.0","id":12,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":uri}}},id_=12)
    print(f"{name:24s} {len(text):7d}B open {t_open:8.1f}ms change {t_change:8.1f}ms definition {t_def:7.2f}ms references {t_ref:8.1f}ms symbols {t_sym:7.2f}ms")
p.stdin.write(frame({"jsonrpc":"2.0","method":"exit"})); p.stdin.flush()
