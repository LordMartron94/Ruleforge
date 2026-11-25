"""Exercise the installed Genesis LSP with the real Ruleforge grammar/mapping."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[4]
server = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='ruleforge-lsp-') as temp:
    directory = Path(temp)
    uri = (directory / 'sample.rf').as_uri()
    source = 'version 1.0.0;\nmodule sample;\nexport func Known() -> int { return 1; }\nexport func main() { let value = Known(); }\n'
    (directory / 'sample.rf').write_text(source)
    (directory / 'genesis.toml').write_text('[lsp]\nwalk=false\ncache=false\n')
    def request(method, params, id=None):
        message = dict(jsonrpc='2.0', method=method, params=params)
        if id is not None:
            message['id'] = id
        data = json.dumps(message).encode()
        return f'Content-Length: {len(data)}\r\n\r\n'.encode() + data
    wire = request('initialize', dict(rootUri=directory.as_uri(), capabilities={}), 1)
    wire += request('initialized', {})
    wire += request('textDocument/didOpen', dict(textDocument=dict(uri=uri, languageId='ruleforge', version=1, text=source)))
    wire += request('textDocument/documentSymbol', dict(textDocument=dict(uri=uri)), 2)
    wire += request('textDocument/definition', dict(textDocument=dict(uri=uri), position=dict(line=3, character=source.splitlines()[3].index('Known') + 1)), 3)
    wire += request('textDocument/didChange', dict(textDocument=dict(uri=uri, version=2), contentChanges=[dict(text='version 1.0.0; module sample; export func main( {')]))
    wire += request('shutdown', {}, 4)
    wire += request('exit', {})
    result = subprocess.run([server, 'lsp', '--config', directory / 'genesis.toml', '-I', root / 'libs/logos/stdlib', root / 'projects/ruleforge/spec/ruleforge.gen'], input=wire, capture_output=True, timeout=30, cwd=directory)
    assert result.returncode == 0, result.stderr.decode()
    data = result.stdout
    messages = []
    while data:
        headers, data = data.split(b'\r\n\r\n', 1)
        length = int(headers.split(b'Content-Length: ')[1].split(b'\r\n')[0])
        messages.append(json.loads(data[:length]))
        data = data[length:]
    responses = {message['id']: message for message in messages if 'id' in message}
    assert responses[1]['result']['capabilities']['definitionProvider']
    assert 'Known' in json.dumps(responses[2]['result']), responses[2]
    assert uri in json.dumps(responses[3]['result']) and '"line": 2' in json.dumps(responses[3]['result']), responses[3]
    assert any(message.get('method') == 'textDocument/publishDiagnostics' and message['params']['diagnostics'] for message in messages), messages
print('PASS: Ruleforge LSP initialization, symbols, definition lookup and syntax diagnostics')
