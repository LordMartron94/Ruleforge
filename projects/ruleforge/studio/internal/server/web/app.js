// Ruleforge Studio: a web editor and inspector over the Ruleforge engine.
//
// Everything that means something comes from the engine: syntax highlighting (its lexer tokens), diagnostics, the filter, the
// block model with traces, simulation, and the theme colors' source positions (its AST). This file only draws it and moves text.

const $ = (selector, root = document) => root.querySelector(selector);

/** Builds an element; strings become text nodes, so file content can never inject markup. */
function h(tag, attributes = {}, ...children) {
  const element = document.createElement(tag);
  for (const [name, value] of Object.entries(attributes)) {
    if (name === 'class') element.className = value;
    else if (name.startsWith('on')) element.addEventListener(name.slice(2), value);
    else if (value === true) element.setAttribute(name, '');
    else if (value !== false && value != null) element.setAttribute(name, value);
  }
  for (const child of children.flat()) if (child != null && child !== false) element.append(child.nodeType ? child : document.createTextNode(String(child)));
  return element;
}

async function api(path, options = {}) {
  const response = await fetch(path, options);
  const type = response.headers.get('content-type') || '';
  const body = type.includes('json') ? await response.json() : await response.text();
  if (!response.ok) throw new Error(body && body.error ? body.error : `${response.status} ${response.statusText}`);
  return body;
}

const state = {
  info: null, files: [], open: null, buffers: new Map(), /* path -> {text, saved, tokens} */
  diagnostics: [], model: null, previous: null, filterText: '', manifest: null, ast: null,
  selectedBlock: null, centerTab: 'editor', rightTab: 'inspector', simResults: null,
};

const setStatus = (text) => { $('#status').textContent = text; };

// ---------------------------------------------------------------- tokens and highlighting

const tokenClass = (kind) => {
  if (kind === 'line_comment' || kind.startsWith('block_comment')) return 'tok-comment';
  if (kind === 'error') return 'tok-error';
  if (kind === 'string_literal') return 'tok-string';
  if (kind === 'integer_literal' || kind === 'float_literal' || kind === 'version') return 'tok-number';
  if (kind === 'hex_color' || kind.startsWith('color_') || kind === 'kw_rgb' || kind === 'kw_hsl' || kind === 'kw_hsv') return 'tok-color';
  if (kind === 'parameter_reference' || kind === 'compiler_reference') return 'tok-ref';
  if (['kw_int', 'kw_float', 'kw_bool', 'kw_string', 'kw_color', 'kw_style', 'kw_list', 'kw_void'].includes(kind)) return 'tok-type';
  if (kind.startsWith('kw_')) return 'tok-kw';
  return '';
};

/** Splits the text into spans by token ranges (bytes -> UTF-16 offsets), marking diagnostic ranges with a squiggle. */
function highlight(text, tokens, diagnostics, file) {
  const encoder = new TextEncoder();
  const bytes = encoder.encode(text);
  const decoder = new TextDecoder();
  const slice = (a, b) => decoder.decode(bytes.subarray(a, b));
  const fragment = document.createDocumentFragment();
  const lineStarts = [0];
  for (let i = 0; i < bytes.length; i++) if (bytes[i] === 10) lineStarts.push(i + 1);
  const squiggles = diagnostics.filter((d) => d.file === file || file.endsWith(d.file.replace(/^\.\//, ''))).map((d) => {
    const start = (lineStarts[d.line - 1] ?? 0) + Math.max(d.column - 1, 0);
    let end = (lineStarts[d.endLine - 1] ?? start) + Math.max(d.endColumn - 1, 0);
    if (end <= start) end = start + 1;
    return { start, end, severity: d.severity };
  });
  const emit = (a, b, cls) => {
    if (b <= a) return;
    // Split at squiggle boundaries so a diagnostic underlines exactly its range.
    const cuts = new Set([a, b]);
    for (const s of squiggles) { if (s.start > a && s.start < b) cuts.add(s.start); if (s.end > a && s.end < b) cuts.add(s.end); }
    const points = [...cuts].sort((x, y) => x - y);
    for (let i = 0; i + 1 < points.length; i++) {
      const hit = squiggles.find((s) => s.start <= points[i] && points[i + 1] <= s.end);
      const classes = [cls, hit ? 'squiggle ' + hit.severity : ''].filter(Boolean).join(' ');
      const piece = slice(points[i], points[i + 1]);
      fragment.append(classes ? h('span', { class: classes }, piece) : document.createTextNode(piece));
    }
  };
  let cursor = 0;
  for (const [start, end, kind] of tokens || []) {
    if (start > cursor) emit(cursor, start, '');
    emit(start, end, tokenClass(kind));
    cursor = end;
  }
  emit(cursor, bytes.length, '');
  fragment.append(document.createTextNode('\n'));
  return fragment;
}

let tokenTimer = null;
function scheduleTokens(path) {
  clearTimeout(tokenTimer);
  tokenTimer = setTimeout(async () => {
    const buffer = state.buffers.get(path);
    if (!buffer || !path.endsWith('.rf')) return;
    try {
      buffer.tokens = await api('/api/tokens', { method: 'POST', body: buffer.text });
    } catch { buffer.tokens = null; }
    if (state.open === path) renderEditorLayer();
  }, 300);
}

// ---------------------------------------------------------------- files

async function loadFiles() {
  state.files = await api('/api/files');
  renderFiles();
}

function renderFiles() {
  const nav = $('#files');
  nav.replaceChildren();
  const groups = { script: 'Scripts', data: 'Data', settings: 'Settings', filter: 'Output' };
  for (const [kind, title] of Object.entries(groups)) {
    const items = state.files.filter((f) => f.kind === kind);
    if (!items.length) continue;
    nav.append(h('div', { class: 'group' }, title));
    for (const file of items) {
      const dirty = state.buffers.get(file.path) && state.buffers.get(file.path).text !== state.buffers.get(file.path).saved;
      nav.append(h('button', { class: file.path === state.open ? 'active' : '', title: file.path, onclick: () => openFile(file.path) }, file.path, dirty ? h('span', { class: 'dirty' }, ' •') : ''));
    }
  }
}

async function openFile(path, line) {
  if (!state.buffers.has(path)) {
    const text = await api('/api/file?path=' + encodeURIComponent(path));
    state.buffers.set(path, { text, saved: text, tokens: null });
    scheduleTokens(path);
  }
  state.open = path;
  state.centerTab = 'editor';
  renderCenter();
  renderFiles();
  if (line) jumpTo(line, 1);
}

function jumpTo(line, column) {
  const area = $('#editor-area');
  if (!area) return;
  const text = area.value.split('\n');
  let offset = 0;
  for (let i = 0; i < line - 1 && i < text.length; i++) offset += text[i].length + 1;
  area.focus();
  area.setSelectionRange(offset + column - 1, offset + (text[line - 1] || '').length);
  area.scrollTop = Math.max((line - 6) * 19.4, 0);
  syncScroll();
}

async function saveFile(path) {
  const buffer = state.buffers.get(path);
  if (!buffer || buffer.text === buffer.saved) return;
  await api('/api/file?path=' + encodeURIComponent(path), { method: 'PUT', body: buffer.text });
  buffer.saved = buffer.text;
}

async function saveAll() {
  for (const path of state.buffers.keys()) await saveFile(path);
  renderFiles();
}

// ---------------------------------------------------------------- editor

function renderEditorLayer() {
  const pre = $('#editor-layer');
  if (!pre || !state.open) return;
  const buffer = state.buffers.get(state.open);
  pre.replaceChildren(highlight(buffer.text, buffer.tokens, state.diagnostics, state.open));
  const gutter = $('#gutter');
  const lines = buffer.text.split('\n').length;
  const errorLines = new Set(state.diagnostics.filter((d) => state.open.endsWith((d.file || '').replace(/^\.\//, ''))).map((d) => d.line));
  gutter.replaceChildren(...Array.from({ length: lines }, (_, i) => h('div', { class: errorLines.has(i + 1) ? 'err' : '' }, i + 1)));
  syncScroll();
}

function syncScroll() {
  const area = $('#editor-area');
  if (!area) return;
  $('#editor-layer').scrollTop = area.scrollTop;
  $('#editor-layer').scrollLeft = area.scrollLeft;
  $('#gutter').scrollTop = area.scrollTop;
}

function renderEditor(body) {
  if (!state.open) { body.append(h('div', { class: 'pad muted' }, 'Open a file from the list.')); return; }
  const buffer = state.buffers.get(state.open);
  const area = h('textarea', { id: 'editor-area', spellcheck: 'false', 'aria-label': 'Source of ' + state.open, autocapitalize: 'off' });
  area.value = buffer.text;
  area.addEventListener('input', () => {
    buffer.text = area.value;
    scheduleTokens(state.open);
    renderEditorLayer();
    renderFiles();
  });
  area.addEventListener('scroll', syncScroll);
  area.addEventListener('keydown', (event) => {
    if (event.key === 'Tab') {
      event.preventDefault();
      area.setRangeText('    ', area.selectionStart, area.selectionEnd, 'end');
      area.dispatchEvent(new Event('input'));
    }
  });
  body.append(h('div', { class: 'editor' }, h('div', { id: 'gutter', class: 'gutter' }), h('div', { class: 'surface' }, h('pre', { id: 'editor-layer', 'aria-hidden': 'true' }), area)));
  renderEditorLayer();
}

// ---------------------------------------------------------------- filter view

function renderFilter(body) {
  if (!state.filterText) { body.append(h('div', { class: 'pad muted' }, 'Compile to see the generated filter.')); return; }
  const blockAt = new Map();
  for (const block of state.model.blocks) for (let line = block.lines[0] - 1; line <= block.lines[1]; line++) blockAt.set(line, block.id);
  const pre = h('pre', { class: 'filter' });
  state.filterText.split('\n').forEach((text, index) => {
    const id = blockAt.get(index + 1);
    const hot = state.selectedBlock != null && id === state.selectedBlock;
    pre.append(h('span', { class: 'line' + (hot ? ' hot' : ''), onclick: () => id && selectBlock(id, { scroll: false }) }, h('span', { class: 'n' }, index + 1), text, '\n'));
  });
  body.append(pre);
  const hot = $('.line.hot', body);
  if (hot) hot.scrollIntoView({ block: 'center' });
}

// ---------------------------------------------------------------- inspector

const rgbToCss = (text) => {
  const parts = text.trim().split(/\s+/).map(Number);
  return parts.length === 4 ? `rgba(${parts[0]},${parts[1]},${parts[2]},${(parts[3] / 255).toFixed(3)})` : `rgb(${parts[0]},${parts[1]},${parts[2]})`;
};

/** A picture of the item label as the game would draw it, from the block's style. */
function labelPreview(style, text) {
  const properties = Object.fromEntries((style?.properties || []).map((p) => [p.name, p.value]));
  const label = h('span', { class: 'label-preview' }, text);
  if (properties.Background) label.style.background = rgbToCss(properties.Background);
  if (properties.Border) label.style.borderColor = rgbToCss(properties.Border);
  label.style.color = properties.Text ? rgbToCss(properties.Text) : '#c8c8c8';
  if (properties.FontSize) label.style.fontSize = Math.round(properties.FontSize * 0.6 + 4) + 'px';
  return label;
}

function conditionText(condition) {
  return `${condition.name} ${condition.op} ${condition.values.map((v) => JSON.stringify(v)).join(' ')}`;
}

function selectBlock(id, { scroll = true } = {}) {
  state.selectedBlock = id;
  state.rightTab = 'inspector';
  renderRight();
  if (state.centerTab === 'filter') renderCenter();
}

function renderInspector(body) {
  if (!state.model) { body.append(h('div', { class: 'pad muted' }, 'Compile to inspect the generated rules.')); return; }
  const filterInput = h('input', { type: 'search', placeholder: 'Filter blocks: name, condition, text…', 'aria-label': 'Filter blocks', value: state.inspectorFilter || '' });
  const list = h('div', { class: 'list' });
  const details = h('div', { class: 'pad' });
  const draw = () => {
    state.inspectorFilter = filterInput.value;
    const needle = filterInput.value.toLowerCase();
    list.replaceChildren();
    const matching = state.model.blocks.filter((b) => !needle || JSON.stringify([b.rule.name, b.role, b.conditions, b.trace]).toLowerCase().includes(needle));
    for (const block of matching.slice(0, 400)) {
      list.append(h('div', { class: 'row' + (block.id === state.selectedBlock ? ' active' : ''), onclick: () => selectBlock(block.id) },
        h('span', { class: 'id' }, '#' + block.id), h('span', { class: 'pill ' + block.action }, block.action),
        h('span', {}, block.rule.name || '(rule)'), h('span', { class: 'muted' }, block.role)));
    }
    if (matching.length > 400) list.append(h('div', { class: 'pad muted' }, `${matching.length - 400} more; narrow the filter.`));
    details.replaceChildren();
    const block = state.model.blocks.find((b) => b.id === state.selectedBlock);
    if (!block) { details.append(h('div', { class: 'muted' }, `${state.model.blocks.length} blocks. Select one to see where it came from.`)); return; }
    details.append(h('h4', {}, `Block #${block.id}: ${block.rule.name || '(rule)'} — ${block.role}`));
    details.append(labelPreview(block.style, block.conditions.find((c) => c.name === 'baseType')?.values[0] || 'Item'));
    details.append(h('dl', { class: 'kv' },
      h('dt', {}, 'action'), h('dd', {}, block.action),
      h('dt', {}, 'declared'), h('dd', {}, h('a', { href: '#', onclick: (e) => { e.preventDefault(); openFile(fileOf(block.rule), block.rule.line); } }, `${block.rule.module} ${block.rule.file}:${block.rule.line}`)),
      h('dt', {}, 'in filter'), h('dd', {}, h('a', { href: '#', onclick: (e) => { e.preventDefault(); state.centerTab = 'filter'; renderCenter(); } }, `lines ${block.lines[0]}–${block.lines[1]}`)),
      h('dt', {}, 'shadowed'), h('dd', {}, block.rule.shadowed ? `${block.rule.shadowed} variant/baseline unreachable after an always-matching variant` : 'nothing')));
    details.append(h('h4', {}, 'Conditions'), block.conditions.length ? h('ul', {}, block.conditions.map((c) => h('li', {}, h('code', {}, conditionText(c))))) : h('div', { class: 'muted' }, 'none: matches every item'));
    if (block.style) details.append(h('h4', {}, `Style ${block.style.theme}.${block.style.key}`), h('ul', {}, block.style.properties.map((p) => h('li', {}, p.name, ' ', h('code', {}, p.value), ' ', /^\d+ \d+ \d+/.test(p.value) ? h('span', { class: 'swatch', style: `background:${rgbToCss(p.value)}` }) : ''))));
    details.append(h('h4', {}, 'How it was generated'));
    details.append(h('ol', { class: 'trace' }, block.trace.map((t) => h('li', { onclick: () => openFile(fileOf(t), t.line) },
      h('strong', {}, t.kind), ' ', t.label || '', h('span', { class: 'muted' }, ` ${t.module}:${t.line}`),
      t.record ? h('pre', { class: 'mono' }, JSON.stringify(t.record, null, 1)) : ''))));
  };
  filterInput.addEventListener('input', draw);
  body.append(h('div', { class: 'pad' }, filterInput), list, details);
  draw();
}

/** The project-relative path of a span's file (the engine reports it as it was opened, e.g. ./filters/a.rf). */
function fileOf(span) {
  const wanted = (span.file || '').replace(/^\.\//, '');
  return state.files.find((f) => f.path === wanted)?.path || wanted;
}

// ---------------------------------------------------------------- simulator

const presets = () => { try { return JSON.parse(localStorage.getItem('ruleforge.items') || '[]'); } catch { return []; } };
const savePresets = (value) => { try { localStorage.setItem('ruleforge.items', JSON.stringify(value)); } catch { /* storage is optional */ } };

function renderSimulator(body) {
  const sample = state.simItem || '{\n  "baseType": "Exalted Orb",\n  "class": "Stackable Currency",\n  "rarity": "Normal",\n  "areaLevel": 83\n}';
  const area = h('textarea', { rows: 9, class: 'mono', style: 'width:100%', 'aria-label': 'Item JSON' });
  area.value = sample;
  const result = h('div', {});
  const run = async () => {
    state.simItem = area.value;
    let items;
    try { items = JSON.parse(area.value); } catch (error) { result.replaceChildren(h('div', { class: 'sev-error' }, 'The item is not valid JSON: ' + error.message)); return; }
    try {
      const response = await api('/api/simulate', { method: 'POST', body: JSON.stringify({ items }) });
      if (!response.ok) { result.replaceChildren(...(response.diagnostics || []).map(diagnosticRow)); return; }
      state.simResults = response.results;
      drawResults();
    } catch (error) { result.replaceChildren(h('div', { class: 'sev-error' }, error.message)); }
  };
  const drawResults = () => {
    result.replaceChildren();
    for (const entry of state.simResults || []) {
      const block = state.model?.blocks.find((b) => b.id === entry.matched);
      result.append(h('div', { class: 'card' },
        h('h4', {}, entry.matched == null ? 'No rule matches: the game shows the item with its defaults.' : `Block #${entry.matched} wins`),
        entry.matched != null ? h('div', {}, h('span', { class: 'pill ' + entry.action }, entry.action), ' ', block ? h('a', { href: '#', onclick: (e) => { e.preventDefault(); selectBlock(block.id); } }, `${block.rule.name || '(rule)'} — ${block.role}`) : '') : '',
        entry.style ? h('div', {}, labelPreview(entry.style, 'Item')) : '',
        h('h4', {}, 'Why earlier blocks did not match'),
        h('ul', {}, entry.considered.slice(-30).map((c) => h('li', {}, h('a', { href: '#', onclick: (e) => { e.preventDefault(); selectBlock(c.id); } }, '#' + c.id), ' ',
          c.failed?.condition ? h('span', { class: 'muted' }, `needs ${conditionText(c.failed.condition)}, item has ${JSON.stringify(c.failed.actual)}`) : 'native block (cannot be evaluated)'))),
        entry.shadowed?.length ? h('div', {}, h('strong', {}, 'Also matches, but shadowed: '), entry.shadowed.map((id) => h('a', { href: '#', onclick: (e) => { e.preventDefault(); selectBlock(id); } }, '#' + id + ' '))) : ''));
    }
  };
  const presetSelect = h('select', { 'aria-label': 'Saved items', onchange: () => { if (presetSelect.value !== '') { area.value = presets()[presetSelect.value].json; } } },
    h('option', { value: '' }, 'Saved items…'), ...presets().map((p, i) => h('option', { value: i }, p.name)));
  body.append(h('div', { class: 'pad' },
    h('div', { class: 'muted' }, 'One item or an array of items. Fields are the game conditions: baseType, class, rarity, itemLevel, areaLevel, quality, stackSize, sockets, identified, corrupted, …'),
    area,
    h('div', { style: 'display:flex;gap:8px;margin:8px 0' }, h('button', { class: 'primary', onclick: run }, 'Run'), presetSelect,
      h('button', { onclick: () => { const name = prompt('Name for this item'); if (name) { savePresets([...presets(), { name, json: area.value }]); renderRight(); } } }, 'Save item')),
    result));
  if (state.simResults) drawResults();
}

// ---------------------------------------------------------------- diagnostics and themes and diff

function diagnosticRow(d) {
  return h('div', { class: 'row', onclick: () => d.file && openFile(fileOf(d), d.line) },
    h('span', { class: 'sev-' + d.severity }, d.severity), h('code', {}, d.code), h('span', {}, d.message, d.note ? h('div', { class: 'muted' }, d.note) : ''),
    h('span', { class: 'muted' }, d.file ? `${d.file}:${d.line}` : ''));
}

function renderDiagnostics(body) {
  if (!state.diagnostics.length) { body.append(h('div', { class: 'pad muted' }, 'No diagnostics.')); return; }
  body.append(h('div', { class: 'list' }, state.diagnostics.map(diagnosticRow)));
}

const hexOf = (rgba) => '#' + [24, 16, 8].map((shift) => ((rgba >>> shift) & 255).toString(16).padStart(2, '0')).join('');

/** Theme colors, edited in place: each color is a source span from the engine's AST, replaced by exact position. */
async function renderThemes(body) {
  body.append(h('div', { class: 'pad muted' }, 'Loading themes…'));
  let response;
  try { response = await api('/api/ast'); } catch (error) { body.replaceChildren(h('div', { class: 'pad sev-error' }, error.message)); return; }
  if (!response.ok) { body.replaceChildren(h('div', { class: 'pad' }, 'The sources do not parse yet; see Diagnostics.')); return; }
  state.ast = response.ast;
  body.replaceChildren();
  const pad = h('div', { class: 'pad' });
  // Color sets: "Palette.GOLD" -> the color node (and its file), so a theme color that names a palette entry edits the palette.
  const palette = new Map();
  for (const file of response.ast.files) for (const node of file.ast.children || []) if (node.kind === 'ColorSet') {
    for (const def of node.children || []) { const color = (def.children || [])[0]; if (color && color.kind === 'Color') palette.set(`${node.text}.${def.text}`, { color, file: file.name }); }
  }
  const literalText = (n) => n.kind === 'Int' || n.kind === 'Float' ? String(n.value) : n.kind === 'String' ? JSON.stringify(n.text) : n.kind === 'Ref' ? n.text : n.kind === 'Bool' ? String(!!n.value) : n.kind === 'Color' ? n.text : '…';
  for (const file of response.ast.files) {
    for (const node of file.ast.children || []) {
      if (node.kind !== 'Theme') continue;
      const card = h('div', { class: 'card' }, h('h4', {}, node.text, node.children?.some((c) => c.kind === 'Extends') ? h('span', { class: 'muted' }, ' extends ' + node.children.find((c) => c.kind === 'Extends').text) : ''));
      for (const style of (node.children || []).filter((c) => c.kind === 'ThemeStyle')) {
        card.append(h('div', {}, h('strong', {}, (style.override ? 'override ' : '') + style.text)));
        for (const stateNode of style.children || []) {
          const line = h('div', { style: 'margin-left:12px' }, h('span', { class: 'muted' }, stateNode.text + ': '));
          for (const property of stateNode.children || []) {
            const args = (property.children || []).filter((c) => c.kind !== 'AlphaOverride');
            const parts = args.map((arg) => {
              const target = arg.kind === 'Color' ? { color: arg, file: file.name } : arg.kind === 'Ref' ? palette.get(arg.text) : null;
              if (!target) return h('code', {}, literalText(arg));
              return h('span', {}, h('input', { type: 'color', value: hexOf(target.color.value), title: arg.kind === 'Ref' ? 'Edits ' + arg.text : 'Edit this color', 'aria-label': `${node.text}.${style.text} ${property.text}`, onchange: (event) => replaceColor(target.file, target.color, event.target.value) }), arg.kind === 'Ref' ? h('span', { class: 'muted' }, ' ' + arg.text) : '');
            });
            line.append(h('span', { style: 'margin-right:12px' }, property.text, ' ', ...parts.flatMap((part, i) => (i ? [', ', part] : [part]))));
          }
          card.append(line);
        }
      }
      pad.append(card);
    }
  }
  if (!pad.children.length) pad.append(h('div', { class: 'muted' }, 'No themes declared.'));
  body.append(pad);
}

async function replaceColor(fileName, color, css) {
  const path = fileOf({ file: fileName });
  if (!state.buffers.has(path)) await openFile(path);
  const buffer = state.buffers.get(path);
  if (buffer.text !== buffer.saved) { setStatus('Save the file before editing a color: positions come from the saved source.'); return; }
  const encoder = new TextEncoder(), decoder = new TextDecoder();
  const bytes = encoder.encode(buffer.text);
  const alpha = color.alpha ? (color.value & 255).toString(16).padStart(2, '0') : '';
  const next = decoder.decode(bytes.subarray(0, color.start)) + css + alpha + decoder.decode(bytes.subarray(color.end));
  buffer.text = next;
  await saveFile(path);
  setStatus('Color changed; compiling…');
  await compile();
  renderCenter();
}

function blockKey(block) { return JSON.stringify([block.rule.module, block.rule.name, block.rule.line, block.role, block.conditions]); }

function renderDiff(body) {
  if (!state.previous || !state.model) { body.append(h('div', { class: 'pad muted' }, 'Compile twice to compare what an edit changed in the generated filter.')); return; }
  const before = new Map(state.previous.blocks.map((b) => [blockKey(b), b]));
  const after = new Map(state.model.blocks.map((b) => [blockKey(b), b]));
  const rows = [];
  for (const [key, block] of after) {
    const old = before.get(key);
    if (!old) rows.push(['diff-add', '+', block]);
    else if (JSON.stringify([old.action, old.style]) !== JSON.stringify([block.action, block.style])) rows.push(['diff-chg', '~', block]);
  }
  for (const [key, block] of before) if (!after.has(key)) rows.push(['diff-del', '−', block]);
  body.append(h('div', { class: 'list' }, rows.length ? rows.map(([cls, sign, block]) => h('div', { class: 'row', onclick: () => after.has(blockKey(block)) && selectBlock(block.id) },
    h('span', { class: cls }, sign), h('span', {}, `${block.rule.name || '(rule)'} ${block.role}`), h('span', { class: 'muted mono' }, block.conditions.map(conditionText).join(' and ')))) : h('div', { class: 'pad muted' }, 'The last compile produced the same blocks.')));
}

// ---------------------------------------------------------------- shell

const centerTabs = [['editor', 'Editor'], ['filter', 'Filter'], ['themes', 'Themes']];
const rightTabs = [['inspector', 'Inspector'], ['simulator', 'Simulator'], ['diagnostics', 'Diagnostics'], ['diff', 'Changes']];

function drawTabs(container, tabs, current, choose) {
  container.replaceChildren(...tabs.map(([id, title]) => h('button', { role: 'tab', 'aria-selected': id === current ? 'true' : 'false', onclick: () => choose(id) },
    title, id === 'diagnostics' && state.diagnostics.length ? ` (${state.diagnostics.length})` : '')));
}

function renderCenter() {
  drawTabs($('#center-tabs'), centerTabs, state.centerTab, (id) => { state.centerTab = id; renderCenter(); });
  const body = $('#center-body');
  body.replaceChildren();
  ({ editor: renderEditor, filter: renderFilter, themes: renderThemes })[state.centerTab](body);
}

function renderRight() {
  drawTabs($('#right-tabs'), rightTabs, state.rightTab, (id) => { state.rightTab = id; renderRight(); });
  const body = $('#right-body');
  body.replaceChildren();
  ({ inspector: renderInspector, simulator: renderSimulator, diagnostics: renderDiagnostics, diff: renderDiff })[state.rightTab](body);
}

async function compile() {
  setStatus('Compiling…');
  try {
    await saveAll();
    const result = await api('/api/compile', { method: 'POST', body: '{}' });
    state.diagnostics = result.diagnostics || [];
    if (result.ok) {
      state.previous = state.model;
      state.model = result.model;
      state.filterText = result.filter;
      setStatus(`Compiled: ${result.model.blocks.length} blocks`);
    } else {
      setStatus('Compile failed: ' + (state.diagnostics.find((d) => d.severity === 'error')?.message || 'see Diagnostics'));
      state.rightTab = 'diagnostics';
    }
  } catch (error) { setStatus(error.message); }
  renderRight();
  renderEditorLayer();
  if (state.centerTab === 'filter') renderCenter();
}

async function checkUpdate() {
  try {
    const result = await api('/api/update/check');
    if (!result.available) return;
    const button = $('#update-button');
    button.hidden = false;
    button.textContent = `Update to ${result.release.version}`;
    button.onclick = async () => {
      button.disabled = true;
      try { await api('/api/update/apply', { method: 'POST', body: '{}' }); button.textContent = 'Installed: restart to use it'; } catch (error) { button.textContent = 'Update failed'; setStatus(error.message); }
    };
  } catch { /* offline or no release yet */ }
}

document.addEventListener('keydown', (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key === 's') { event.preventDefault(); if (state.open) saveFile(state.open).then(renderFiles); }
  if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') { event.preventDefault(); compile(); }
});
window.addEventListener('beforeunload', (event) => { for (const b of state.buffers.values()) if (b.text !== b.saved) { event.preventDefault(); return ''; } });

async function start() {
  $('#compile-button').onclick = compile;
  $('#save-button').onclick = () => state.open && saveFile(state.open).then(renderFiles);
  try {
    state.info = await api('/api/info');
    $('#title').textContent = 'Ruleforge Studio — ' + state.info.project;
    $('#entry').textContent = `${state.info.entry} · studio ${state.info.version} · ${state.info.engine}`;
    await loadFiles();
    await openFile(state.info.entry);
  } catch (error) { setStatus(error.message); }
  renderCenter();
  renderRight();
  checkUpdate();
}
start();
