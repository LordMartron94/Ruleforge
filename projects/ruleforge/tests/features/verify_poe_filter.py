"""Parse emitted filters and exercise newline/operand failures with Genesis."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[4]
genesis = str(Path(sys.argv[1]).resolve())
grammar = root / 'projects/ruleforge/spec/PoEFilter.gen'
valid = '''# [Currency] Section comment
Import "extra.filter" Optional
Show # inline comment
    Class == "Stackable Currency"
    BaseType "Maelström Token #1"
    HasExplicitMod >=2 "of Haast" "of Ephij"
    HasInfluence Redeemer
    SocketGroup >= 5GGG
    Sockets RGB
    Rarity <= Rare
    SetTextColor 255 128 0
    SetBackgroundColor 16 35 79 255
    PlayAlertSoundPositional 6 300
    CustomAlertSoundOptional "sounds/drop.ogg" 200
    MinimapIcon 0 Red Star
    PlayEffect Blue Temp
    Continue
Hide
    Fated False
    UnidentifiedItemTier < 3
    DisableDropSound True
Minimal
    Quality > 10'''
with tempfile.TemporaryDirectory(prefix='poe-filter-grammar-') as directory:
    path = Path(directory) / 'fixture.filter'
    for ending in ['\n', '\r\n', '\r']:
        path.write_bytes(valid.replace('\n', ending).encode())
        result = subprocess.run([genesis, 'check', grammar, path], capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
    for source in ['', '  \n # comment', 'Show', 'Show\nSetTextColor 1 2 3 4\n']:
        path.write_text(source)
        result = subprocess.run([genesis, 'check', grammar, path], capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
    for source in ['Show\nSetTextColor 1 2\n', 'Show\nFated Maybe\n',
                   'Show\nUnidentifiedItemTier >=\n', 'Show\nUnknownCommand 4\n',
                   'Show\nBaseType "unterminated\nHide', 'Show Hide\n']:
        path.write_text(source)
        result = subprocess.run([genesis, 'check', grammar, path], capture_output=True)
        assert result.returncode != 0, source
    outputs = list((root / 'personal-filters/out').glob('*.filter'))
    if outputs:
        result = subprocess.run([genesis, 'check', grammar, *outputs], capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
# Load both project grammars together, as the Sublime client does. A malformed
# .filter document must receive syntax diagnostics from the PoE grammar.
with tempfile.TemporaryDirectory(prefix='poe-filter-lsp-') as directory:
    directory = Path(directory)
    config = directory / 'genesis.toml'
    config.write_text('[lsp]\nwalk=false\ncache=false\n')
    uri = (directory / 'sample.filter').as_uri()
    def message(method, params, id=None):
        body = dict(jsonrpc='2.0', method=method, params=params)
        if id is not None: body['id'] = id
        encoded = json.dumps(body).encode()
        return f'Content-Length: {len(encoded)}\r\n\r\n'.encode() + encoded
    wire = message('initialize', dict(rootUri=directory.as_uri(), capabilities={}), 1)
    wire += message('initialized', {})
    wire += message('textDocument/didOpen', dict(textDocument=dict(uri=uri, languageId='poe-filter', version=1, text='Show\nSetTextColor 1 2 3\n')))
    wire += message('textDocument/didChange', dict(textDocument=dict(uri=uri, version=2), contentChanges=[dict(text='Show\nUnknownCommand 4\n')]))
    wire += message('shutdown', {}, 2)
    wire += message('exit', {})
    result = subprocess.run([genesis, 'lsp', '--config', config, '-I', root / 'libs/logos/stdlib', root / 'projects/ruleforge/spec/ruleforge.gen', grammar], input=wire, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stderr
    data = result.stdout
    diagnostics = []
    while data:
        headers, data = data.split(b'\r\n\r\n', 1)
        length = int(headers.split(b'Content-Length: ')[1].split(b'\r\n')[0])
        item = json.loads(data[:length]); data = data[length:]
        if item.get('method') == 'textDocument/publishDiagnostics':
            diagnostics.append(item['params']['diagnostics'])
    assert diagnostics and not diagnostics[0] and diagnostics[-1], diagnostics
print('PASS: PoE filter grammar, operand failures, all newline forms emitted filters and LSP diagnostics')
