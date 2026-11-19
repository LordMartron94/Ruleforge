/* Ruleforge Studio editors: themes, stylesheets and scripts.
   They need the Studio server (ruleforge -studio), which checks every change
   with the real compiler before it writes a file. */
(() => {
'use strict';
if (!DATA) return;
if (!DATA.server) {
  const note = document.getElementById('views-note');
  if (note) note.innerHTML = 'Edit themes, stylesheets and scripts: run <code>./run.sh -studio -open</code>';
  return;
}

/* ---------- shared ---------- */
function h(tag, props, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (k in el && k !== 'list' && k !== 'form') el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    el.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return el;
}
const clone = v => v === undefined ? undefined : JSON.parse(JSON.stringify(v));
// add and fill append children, skipping the null, undefined and false that
// conditional parts leave (the DOM would print them as text).
const kids = children => children.flat(Infinity).filter(c => c !== null && c !== undefined && c !== false);
function add(el, ...children) { el.append(...kids(children)); return el; }
function fill(el, ...children) { el.replaceChildren(...kids(children)); return el; }

async function api(method, path, body) {
  const res = await fetch(path, {
    method,
    headers: { 'X-Ruleforge-Token': DATA.server.token, 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let data;
  try { data = await res.json(); } catch (e) { data = { ok: false, error: `The Studio server answered ${res.status}. Is it still running?` }; }
  if (!res.ok || data.ok === false) throw new Error(data.error || `Request failed (${res.status})`);
  return data;
}

let catalogPromise = null;
const catalog = () => catalogPromise || (catalogPromise = api('GET', '/api/catalog'));
let themesPromise = null;
const themesList = (fresh) => (!fresh && themesPromise) || (themesPromise = api('GET', '/api/themes'));

function datalist(id, values) {
  let list = document.getElementById(id);
  if (!list) { list = h('datalist', { id }); add(document.body, list); }
  const key = values.length + ':' + (values[0] || '') + ':' + (values[values.length - 1] || '');
  if (list.dataset.key !== key) {
    fill(list, ...values.map(v => h('option', { value: v })));
    list.dataset.key = key;
  }
  return id;
}

function setStatus(el, message, level) {
  if (!el) return;
  el.className = 'edt-status' + (level ? ' ' + level : '');
  el.textContent = message || '';
}

const dirtyEditors = new Set();
window.addEventListener('beforeunload', e => {
  if (dirtyEditors.size) { e.preventDefault(); e.returnValue = ''; }
});
const EDITOR_NAMES = { themes: 'Themes', stylesheets: 'Stylesheets', scripts: 'Scripts' };
// mayReload asks before a save's reload would discard another editor's changes.
function mayReload(editor) {
  const others = [...dirtyEditors].filter(e => e !== editor).map(e => EDITOR_NAMES[e] || e);
  return !others.length || confirm(`Saving reloads the Studio, which discards your unsaved changes in ${others.join(' and ')}. Save anyway?`);
}
function reloadInto(view) {
  dirtyEditors.clear();
  state.view = view; persist();
  location.reload();
}
function confirmDiscard(editor) {
  return !dirtyEditors.has(editor) || confirm('Discard your unsaved changes?');
}

function showDialog(title, body, actions = []) {
  const dialog = h('dialog', { class: 'edt-dialog' },
    h('div', { class: 'edt-bar' }, h('h3', { text: title }), h('span', { class: 'grow' }), actions,
      h('button', { class: 'btn', type: 'button', text: 'Close', onclick: () => dialog.close() })),
    h('div', { class: 'body' }, body));
  add(document.body, dialog);
  dialog.addEventListener('close', () => dialog.remove());
  dialog.showModal();
  return dialog;
}

// lineDiff lists the lines each side keeps, drops and adds.
function lineDiff(before, after) {
  // The final line break ends the last line; it is not a line of its own.
  const lines = text => text === '' ? [] : text.replace(/\n$/, '').split('\n');
  const a = lines(before), b = lines(after);
  let start = 0;
  while (start < a.length && start < b.length && a[start] === b[start]) start++;
  let end = 0;
  while (end < a.length - start && end < b.length - start && a[a.length - 1 - end] === b[b.length - 1 - end]) end++;
  const ma = a.slice(start, a.length - end), mb = b.slice(start, b.length - end);
  const ops = a.slice(0, start).map(t => ['=', t]);
  if (ma.length * mb.length > 6e6) {
    ma.forEach(t => ops.push(['-', t])); mb.forEach(t => ops.push(['+', t]));
  } else {
    const n = ma.length, m = mb.length, w = m + 1;
    const lcs = new Uint32Array((n + 1) * w);
    for (let i = n - 1; i >= 0; i--) for (let j = m - 1; j >= 0; j--) {
      lcs[i * w + j] = ma[i] === mb[j] ? lcs[(i + 1) * w + j + 1] + 1 : Math.max(lcs[(i + 1) * w + j], lcs[i * w + j + 1]);
    }
    let i = 0, j = 0;
    while (i < n || j < m) {
      if (i < n && j < m && ma[i] === mb[j]) { ops.push(['=', ma[i]]); i++; j++; }
      else if (i < n && (j === m || lcs[(i + 1) * w + j] >= lcs[i * w + j + 1])) { ops.push(['-', ma[i]]); i++; }
      else { ops.push(['+', mb[j]]); j++; }
    }
  }
  a.slice(a.length - end).forEach(t => ops.push(['=', t]));
  return ops;
}
function diffView(before, after) {
  const ops = lineDiff(before, after);
  const pre = h('pre', { class: 'diff' });
  const changed = ops.map(op => op[0] !== '=');
  const near = ops.map((_, i) => changed.slice(Math.max(0, i - 3), i + 4).some(Boolean));
  let added = 0, removed = 0, skipped = 0, lineA = 0, lineB = 0;
  ops.forEach(([kind, text], i) => {
    if (kind !== '+') lineA++;
    if (kind !== '-') lineB++;
    if (kind === '+') added++;
    if (kind === '-') removed++;
    if (!near[i]) { skipped++; return; }
    if (skipped) { add(pre, h('div', { class: 'gap', text: `… ${skipped} unchanged line${skipped === 1 ? '' : 's'} …` })); skipped = 0; }
    add(pre, h('div', { class: kind === '+' ? 'add' : kind === '-' ? 'del' : 'ctx', text: `${kind === '+' ? '+' : kind === '-' ? '-' : ' '} ${String(kind === '+' ? lineB : lineA).padStart(4)}  ${text}` }));
  });
  if (skipped) add(pre, h('div', { class: 'gap', text: `… ${skipped} unchanged line${skipped === 1 ? '' : 's'} …` }));
  const summary = added || removed ? `${removed} line${removed === 1 ? '' : 's'} removed, ${added} added` : 'No lines change.';
  return h('div', { class: 'stack', style: { gap: '8px' } }, h('p', { class: 'muted', text: summary }), added || removed ? pre : null);
}

function describeResult(result) {
  const lines = [];
  if (result.filters && result.filters.length) {
    const byScript = new Map();
    result.filters.forEach(f => { if (!byScript.has(f.script)) byScript.set(f.script, []); byScript.get(f.script).push(`${f.style} ${f.blocks}`); });
    for (const [script, styles] of byScript) lines.push(`${script} compiles: ${styles.join(', ')} blocks`);
  }
  (result.warnings || []).forEach(w => lines.push('Note: ' + w));
  return lines.join('\n');
}

const tools = (...buttons) => h('span', { class: 'edt-tools' }, buttons);
const iconButton = (text, title, onclick, disabled) => h('button', { class: 'icon', type: 'button', text, title, 'aria-label': title, onclick, disabled });

function editorBar(editor, { onCheck, onSave, onDiscard, onChanges }) {
  const status = h('div', { class: 'edt-status', role: 'status' });
  const bar = h('div', { class: 'edt-bar' },
    h('button', { class: 'btn', type: 'button', text: 'Check', title: 'Compile with this change without saving', onclick: onCheck }),
    h('button', { class: 'btn', type: 'button', text: 'View changes', title: 'Show the file lines this save changes', onclick: onChanges }),
    h('button', { class: 'btn primary', type: 'button', text: 'Save', title: 'Check, write the file, recompile every filter and reload the Studio', onclick: onSave }),
    h('button', { class: 'btn', type: 'button', text: 'Discard', onclick: onDiscard }));
  return { bar, status };
}

/* ---------- palette view: save colours to the theme file ---------- */
{
  const status = document.getElementById('status');
  const save = h('button', {
    class: 'btn', type: 'button', text: 'Save colours to the theme file', onclick: async () => {
      const style = currentStyle(), edits = state.edits[style.id] || {};
      if (!Object.keys(edits).length) { status.textContent = 'Nothing changed yet.'; return; }
      if (!mayReload('palette')) return;
      status.textContent = 'Checking and saving…';
      try {
        const theme = await api('GET', `/api/themes/${encodeURIComponent(style.id)}`);
        theme.groups.forEach(g => g.colours.forEach(c => { if (c.name in edits) c.value = edits[c.name]; }));
        await api('PUT', `/api/themes/${encodeURIComponent(style.id)}`, { theme, hash: theme.hash });
        delete state.edits[style.id]; persist();
        reloadInto('palette');
      } catch (e) { status.textContent = e.message; }
    },
  });
  document.getElementById('copy-changes').after(save);
}

/* ---------- themes ---------- */
const HEX = /^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$/;
const COLOUR_NAME = /^[A-Za-z0-9_-]+$/;
const themesPanel = addView({ id: 'themes', label: 'Themes', render: () => renderThemes() });
const te = { list: null, id: null, theme: null, original: '', create: false, from: '', newId: '', sheetUse: new Map(), lastResult: null };

function themeDirty() { return te.theme && (te.create || JSON.stringify(te.theme) !== te.original); }
function themeChanged() {
  if (themeDirty()) dirtyEditors.add('themes'); else dirtyEditors.delete('themes');
  const flag = document.getElementById('te-dirty');
  if (flag) flag.textContent = themeDirty() ? 'Unsaved changes' : '';
}
function paletteOf(theme) {
  const P = {};
  (theme.groups || []).forEach(g => g.colours.forEach(c => { P[c.name] = c.value; }));
  return P;
}
const sheetFileName = path => (path || '').split('/').pop();

async function stylesheetUse(file) {
  if (!file) return null;
  if (!te.sheetUse.has(file)) {
    te.sheetUse.set(file, api('GET', `/api/stylesheets/${encodeURIComponent(file)}`).then(sheet => {
      const used = new Set();
      sheet.entries.forEach(e => ['TextColor', 'BorderColor', 'BackgroundColor'].forEach(k => {
        const v = e.props[k]; if (v && !v.startsWith('#')) used.add(v);
      }));
      (sheet.distinct || []).forEach(g => (g.vars || []).forEach(v => used.add(v)));
      return used;
    }).catch(() => null));
  }
  return te.sheetUse.get(file);
}

function openTheme(theme, { create = false, from = '', newId = '' } = {}) {
  te.theme = clone(theme);
  te.id = create ? newId : theme.id;
  te.create = create; te.from = from; te.newId = newId;
  te.original = create ? '' : JSON.stringify(te.theme);
  te.lastResult = null;
  if (!create) store.set('editor:theme', theme.id);
  themeChanged();
}

async function renderThemes() {
  if (!te.list) {
    fill(themesPanel, h('p', { class: 'muted', text: 'Loading themes…' }));
    try { te.list = await themesList(); } catch (e) { fill(themesPanel, h('p', { class: 'edt-status bad', text: e.message })); return; }
  }
  if (!te.theme) {
    const want = store.get('editor:theme', state.style);
    openTheme(te.list.themes.find(t => t.id === want) || te.list.themes[0]);
  }
  drawThemes();
}

function drawThemes() {
  const t = te.theme;
  const { bar, status } = editorBar('themes', {
    onCheck: () => saveTheme(true, status),
    onSave: () => saveTheme(false, status),
    onChanges: () => themeChanges(status),
    onDiscard: () => {
      if (!confirmDiscard('themes')) return;
      const saved = te.list.themes.find(x => x.id === (te.create ? te.from : te.id)) || te.list.themes[0];
      openTheme(saved); drawThemes();
    },
  });
  const picker = h('select', {
    class: 'edt', 'aria-label': 'Theme', onchange: e => {
      if (!confirmDiscard('themes')) { e.target.value = te.create ? '' : te.id; return; }
      openTheme(te.list.themes.find(x => x.id === e.target.value)); drawThemes();
    },
  }, te.list.themes.map(x => h('option', { value: x.id, text: `${x.name}${x.enabled ? '' : ' (disabled)'}`, selected: !te.create && x.id === te.id })),
  te.create ? h('option', { value: '', text: `New: ${t.name}`, selected: true }) : null);

  fill(themesPanel, 
    h('div', { class: 'edt-bar' }, h('label', {}, 'Theme', picker),
      h('button', { class: 'btn', type: 'button', text: 'New theme…', onclick: newThemeDialog }),
      h('span', { class: 'grow' }), h('span', { class: 'edt-dirty', id: 'te-dirty' }), bar),
    status,
    h('div', { class: 'edt-layout wide-side' }, h('div', { class: 'stack' }, themeSettings(t), themeSheetCheck(t)), h('div', { class: 'edt-main' }, themePreview(t), themePalette(t))));
  themeChanged();
}

function field(label, control, help) {
  return h('label', { class: 'edt-field', title: help || '' }, h('span', { text: label }), control);
}

function themeSettings(t) {
  const input = (key, placeholder) => h('input', { class: 'edt', value: t[key] || '', placeholder, oninput: e => { t[key] = e.target.value; themeChanged(); } });
  const select = (key, options) => h('select', { class: 'edt', onchange: e => { t[key] = e.target.value; themeChanged(); if (key === 'stylesheet') drawThemes(); } },
    [...new Set([t[key], ...options])].filter(Boolean).map(v => h('option', { value: v, text: v, selected: v === t[key] })));
  return h('div', { class: 'edt-card' }, h('h3', { text: 'Settings' }),
    te.create ? field('File', h('input', { class: 'edt mono', value: te.newId, oninput: e => { te.newId = te.id = e.target.value.trim(); } }), 'The style file name, without .toml') : h('p', { class: 'hint', text: t.file }),
    field('Name', input('name', 'Shown in logs and the Studio')),
    field('Suffix', input('suffix', t.name), 'Added to each filter file name: "<script> (<suffix>).filter"'),
    h('label', { class: 'edt-row' }, h('input', { type: 'checkbox', checked: t.enabled, onchange: e => { t.enabled = e.target.checked; themeChanged(); } }), 'Compile filters with this theme'),
    field('Stylesheet', select('stylesheet', te.list.stylesheets)),
    field('Base types CSV', select('baseTypes', te.list.baseTypes)));
}

function themeSheetCheck(t) {
  const card = h('div', { class: 'edt-card' }, h('h3', { text: 'Stylesheet colours' }), h('p', { class: 'hint', text: 'Checking which colours the stylesheet uses…' }));
  stylesheetUse(sheetFileName(t.stylesheet)).then(used => {
    if (!used) { card.lastChild.textContent = 'The stylesheet could not be read.'; return; }
    const P = paletteOf(t);
    const missing = [...used].filter(n => !(n in P)).sort();
    const unused = Object.keys(P).filter(n => !used.has(n)).sort();
    fill(card, h('h3', { text: 'Stylesheet colours' }),
      missing.length
        ? h('div', { class: 'stack', style: { gap: '6px' } }, h('p', { class: 'edt-status bad', text: `${missing.length} colour${missing.length === 1 ? '' : 's'} the stylesheet uses ${missing.length === 1 ? 'is' : 'are'} missing; the theme cannot compile without ${missing.length === 1 ? 'it' : 'them'}.` }),
          h('div', { class: 'edt-chips' }, missing.map(n => h('span', { text: n }))),
          h('button', { class: 'btn small', type: 'button', text: 'Add them in grey', onclick: () => {
            t.groups.push({ title: 'Needed by the stylesheet', blank: t.groups.length ? 1 : 0, colours: missing.map(name => ({ name, value: '#808080' })) });
            themeChanged(); drawThemes();
          } }))
        : h('p', { class: 'edt-status ok', text: `All ${used.size} colours the stylesheet uses are here.` }),
      unused.length ? h('div', { class: 'stack', style: { gap: '6px' } }, h('p', { class: 'hint', text: 'In the palette but not used by the stylesheet:' }), h('div', { class: 'edt-chips' }, unused.map(n => h('span', { text: n })))) : null);
  });
  return card;
}

function themePreview(t) {
  const card = h('div', { class: 'edt-card' }, h('h3', { text: 'Preview' }));
  const style = DATA.styles.find(s => s.id === te.id);
  if (!style) {
    add(card, h('p', { class: 'hint', text: 'A preview appears once this theme is saved, enabled and compiled.' }));
    return card;
  }
  const stage = h('div', { class: 'edt-preview', id: 'te-preview' });
  add(card, h('p', { class: 'hint', text: 'A sample of this theme\'s styles, from the most prominent down, drawn with the colours below.' }), stage);
  drawThemePreview(stage, style, t);
  return card;
}
function drawThemePreview(stage, style, t) {
  const P = paletteOf(t);
  const seen = new Set();
  const looks = style.looks.filter(l => l.name.startsWith('Final/') && l.look.text).sort((a, b) => (b.look.fontSize || 0) - (a.look.fontSize || 0));
  fill(stage, );
  for (const entry of looks) {
    const key = [entry.look.text && entry.look.text.var, entry.look.background && entry.look.background.var, entry.look.border && entry.look.border.var, entry.look.fontSize].join('|');
    if (seen.has(key)) continue;
    seen.add(key);
    add(stage, labelEl(styleCategory(entry.name).leaf, entry.look, P, 'Normal', 0.85));
    if (seen.size >= 36) break;
  }
}
let previewTimer = 0;
function refreshThemePreview() {
  clearTimeout(previewTimer);
  previewTimer = setTimeout(() => {
    const stage = document.getElementById('te-preview'), style = DATA.styles.find(s => s.id === te.id);
    if (stage && style) drawThemePreview(stage, style, te.theme);
  }, 80);
}

function themePalette(t) {
  const card = h('div', { class: 'edt-card' }, h('h3', { text: 'Palette' }),
    h('p', { class: 'hint', text: 'Colour names are what the stylesheet refers to. Headings group the colours in the file and in the palette view. Values are #RRGGBB, or #RRGGBBAA with transparency.' }));
  const names = () => t.groups.flatMap(g => g.colours.map(c => c.name));
  t.groups.forEach((g, gi) => {
    if (!g.colours.length && !g.title && !(g.comments || []).length) return; // empty lines at the end of the table
    const rows = h('div', { class: 'edt-colours' });
    g.colours.forEach((c, ci) => {
      const swatch = h('input', { type: 'color', value: HEX.test(c.value) ? c.value.slice(0, 7).toLowerCase() : '#808080', 'aria-label': `${c.name} colour` });
      const name = h('input', { class: 'edt mono', value: c.name, 'aria-label': 'Colour name' });
      const hex = h('input', { class: 'edt mono', value: c.value, 'aria-label': `${c.name} value` });
      const check = () => {
        const dup = names().filter(n => n === c.name).length > 1;
        name.classList.toggle('invalid', !COLOUR_NAME.test(c.name) || dup);
        name.title = dup ? 'Another colour has this name' : COLOUR_NAME.test(c.name) ? '' : 'Use letters, digits, - and _';
        hex.classList.toggle('invalid', !HEX.test(c.value));
      };
      swatch.addEventListener('input', () => { c.value = (swatch.value + (c.value.length === 9 ? c.value.slice(7) : '')).toUpperCase(); hex.value = c.value; check(); themeChanged(); refreshThemePreview(); });
      name.addEventListener('input', () => { c.name = name.value.trim(); check(); themeChanged(); });
      hex.addEventListener('input', () => { c.value = hex.value.trim().toUpperCase(); if (HEX.test(c.value)) swatch.value = c.value.slice(0, 7).toLowerCase(); check(); themeChanged(); refreshThemePreview(); });
      check();
      add(rows, h('div', { class: 'edt-colour' }, swatch, name, hex, iconButton('×', `Remove ${c.name}`, () => { g.colours.splice(ci, 1); themeChanged(); drawThemes(); })));
    });
    const move = d => { const j = gi + d; if (j < 0 || j >= t.groups.length) return; [t.groups[gi], t.groups[j]] = [t.groups[j], t.groups[gi]]; themeChanged(); drawThemes(); };
    add(card, h('div', { class: 'edt-group' },
      h('div', { class: 'edt-row' },
        h('input', { class: 'edt fill', value: g.title, placeholder: '(no heading)', 'aria-label': 'Group heading', oninput: e => { g.title = e.target.value; themeChanged(); } }),
        h('span', { class: 'mono muted', text: `${g.colours.length}` }),
        tools(iconButton('↑', 'Move the group up', () => move(-1), gi === 0), iconButton('↓', 'Move the group down', () => move(1), gi === t.groups.length - 1),
          iconButton('×', 'Remove the group and its colours', () => { if (!g.colours.length || confirm(`Remove "${g.title || 'this group'}" and its ${g.colours.length} colours?`)) { t.groups.splice(gi, 1); themeChanged(); drawThemes(); } }))),
      rows,
      h('div', {}, h('button', { class: 'btn small', type: 'button', text: 'Add colour', onclick: () => {
        let n = 1; while (names().includes(`new-colour-${n}`)) n++;
        g.colours.push({ name: `new-colour-${n}`, value: '#808080' }); themeChanged(); drawThemes();
      } }))));
  });
  add(card, h('div', {}, h('button', { class: 'btn', type: 'button', text: 'Add group', onclick: () => {
    const trailing = t.groups.length && !t.groups[t.groups.length - 1].colours.length && !t.groups[t.groups.length - 1].title ? 1 : 0;
    t.groups.splice(t.groups.length - trailing, 0, { title: 'New group', blank: t.groups.length ? 1 : 0, colours: [] });
    themeChanged(); drawThemes();
  } })));
  return card;
}

function themeProblems(t) {
  const problems = [];
  if (!t.name.trim()) problems.push('The theme needs a name.');
  if (te.create && !/^[A-Za-z0-9][A-Za-z0-9._ -]*$/.test(te.newId)) problems.push('The file name may use letters, digits, spaces, . _ and -.');
  const names = new Set();
  t.groups.forEach(g => g.colours.forEach(c => {
    if (!COLOUR_NAME.test(c.name)) problems.push(`"${c.name}" is not a valid colour name.`);
    else if (names.has(c.name)) problems.push(`"${c.name}" is in the palette twice.`);
    names.add(c.name);
    if (!HEX.test(c.value)) problems.push(`${c.name}: "${c.value}" is not #RRGGBB or #RRGGBBAA.`);
  }));
  return problems;
}

function themeRequest(dryRun) {
  return { theme: te.theme, hash: te.create ? '' : te.theme.hash, create: te.create, from: te.from, dryRun };
}
async function saveTheme(dryRun, status) {
  const problems = themeProblems(te.theme);
  if (problems.length) { setStatus(status, problems.join('\n'), 'bad'); return; }
  if (!dryRun && !mayReload('themes')) return;
  setStatus(status, dryRun ? 'Checking: compiling every script with this theme…' : 'Saving: checking, writing and recompiling every filter…');
  try {
    const result = await api('PUT', `/api/themes/${encodeURIComponent(te.id)}`, themeRequest(dryRun));
    te.lastResult = result;
    if (dryRun) { setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok'); return; }
    store.set('editor:theme', te.id);
    reloadInto('themes');
  } catch (e) { setStatus(status, e.message, 'bad'); }
}
async function themeChanges(status) {
  const problems = themeProblems(te.theme);
  if (problems.length) { setStatus(status, problems.join('\n'), 'bad'); return; }
  setStatus(status, 'Preparing the file…');
  try {
    const result = await api('PUT', `/api/themes/${encodeURIComponent(te.id)}`, themeRequest(true));
    setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok');
    showDialog(`Changes to ${te.id}.toml`, diffView(result.original, result.contents));
  } catch (e) { setStatus(status, e.message, 'bad'); }
}

function newThemeDialog() {
  if (!confirmDiscard('themes')) return;
  const name = h('input', { class: 'edt', placeholder: 'Aryzel V: The Crowned King' });
  const file = h('input', { class: 'edt mono', placeholder: 'aryzel-5-crowned-king' });
  let fileEdited = false;
  file.addEventListener('input', () => { fileEdited = true; });
  name.addEventListener('input', () => { if (!fileEdited) file.value = name.value.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, ''); });
  const copyOf = h('select', { class: 'edt' }, te.list.themes.map(x => h('option', { value: x.id, text: x.name, selected: x.id === te.id })));
  const sheet = h('select', { class: 'edt' }, te.list.stylesheets.map(s => h('option', { value: s, text: s })));
  const mode = h('select', { class: 'edt' }, h('option', { value: 'copy', text: 'A copy of an existing theme' }), h('option', { value: 'blank', text: 'Blank, for a stylesheet' }));
  const copyRow = field('Copy of', copyOf), sheetRow = field('Stylesheet', sheet);
  const sync = () => { copyRow.hidden = mode.value !== 'copy'; sheetRow.hidden = mode.value !== 'blank'; };
  mode.addEventListener('change', sync); sync();
  const problem = h('p', { class: 'edt-status bad' });
  const dialog = showDialog('New theme', h('div', { class: 'edt-grid' }, field('Name', name), field('File name', file), field('Start from', mode), copyRow, sheetRow, problem), [
    h('button', { class: 'btn primary', type: 'button', text: 'Create', onclick: async () => {
      const id = file.value.trim();
      if (!name.value.trim() || !/^[A-Za-z0-9][A-Za-z0-9._ -]*$/.test(id)) { problem.textContent = 'Give the theme a name and a file name (letters, digits, spaces, . _ -).'; return; }
      if (te.list.themes.some(x => x.id === id)) { problem.textContent = `${id}.toml already exists.`; return; }
      let theme;
      if (mode.value === 'copy') {
        theme = clone(te.list.themes.find(x => x.id === copyOf.value));
        theme.name = name.value.trim(); theme.suffix = ''; theme.enabled = true; theme.hash = '';
        openTheme(theme, { create: true, from: copyOf.value, newId: id });
      } else {
        const used = await stylesheetUse(sheetFileName(sheet.value));
        theme = { id, file: id + '.toml', name: name.value.trim(), suffix: '', enabled: false, stylesheet: sheet.value, baseTypes: te.list.baseTypes[0] || '', hash: '',
          groups: [{ title: 'Colours', blank: 0, colours: [...(used || [])].sort().map(n => ({ name: n, value: '#808080' })) }] };
        openTheme(theme, { create: true, from: '', newId: id });
      }
      dialog.close(); drawThemes();
    } }),
  ]);
  name.focus();
}

/* ---------- stylesheets ---------- */
const sheetsPanel = addView({ id: 'stylesheets', label: 'Stylesheets', render: () => renderSheets() });
const se = { list: null, file: null, hash: '', entries: [], byName: new Map(), original: new Map(), distinct: [], distinctOriginal: '', selected: null, search: '', preview: '', create: false, from: '' };
const PROP_ORDER = ['Comment', 'Combination', 'TextColor', 'BorderColor', 'BackgroundColor', 'FontSize', 'Minimap', 'Beam', 'DropSound', 'DropVolume'];
const STYLE_NAME = /^[A-Za-z0-9_-]+(\/[A-Za-z0-9_-]+)*$/;

function entryDirty(e) { return e.isNew || e.deleted || JSON.stringify(cleanProps(e.props)) !== se.original.get(e.name); }
function sheetDirty() { return se.create || se.entries.some(entryDirty) || JSON.stringify(se.distinct) !== se.distinctOriginal; }
function sheetChanged() {
  if (sheetDirty()) dirtyEditors.add('stylesheets'); else dirtyEditors.delete('stylesheets');
  const flag = document.getElementById('se-dirty');
  if (flag) { const n = se.entries.filter(entryDirty).length; flag.textContent = sheetDirty() ? `Unsaved changes${n ? ` in ${n} style${n === 1 ? '' : 's'}` : ''}` : ''; }
}
function cleanProps(props) {
  const out = {};
  for (const key of PROP_ORDER) {
    const v = props[key];
    if (v === undefined || v === null) continue;
    if (key === 'Minimap' || key === 'Beam') {
      const sub = {};
      Object.entries(v).forEach(([k, x]) => { if (x !== undefined && x !== null && x !== '') sub[k] = x; });
      if (Object.keys(sub).length) out[key] = sub;
    } else out[key] = v;
  }
  if (props.other && Object.keys(props.other).length) out.other = props.other;
  return out;
}

async function loadSheet(file, { create = false, from = '' } = {}) {
  se.create = create; se.from = from; se.file = file;
  const sheet = create && !from ? { entries: [], distinct: [], hash: '' } : await api('GET', `/api/stylesheets/${encodeURIComponent(create ? from : file)}`);
  se.hash = create ? '' : sheet.hash;
  se.entries = sheet.entries.map(e => ({ name: e.name, comments: e.comments || [], props: clone(e.props) }));
  se.byName = new Map(se.entries.map(e => [e.name, e]));
  se.original = new Map(sheet.entries.map(e => [e.name, JSON.stringify(cleanProps(e.props))]));
  se.distinct = clone(sheet.distinct || []);
  se.distinctOriginal = JSON.stringify(se.distinct);
  const want = store.get('editor:style', '');
  se.selected = se.byName.has(want) ? want : (se.entries.find(e => e.name.startsWith('Final/')) || se.entries[0] || {}).name || null;
  if (!create) store.set('editor:stylesheet', file);
  sheetChanged();
}

async function renderSheets() {
  if (!se.list) {
    fill(sheetsPanel, h('p', { class: 'muted', text: 'Loading stylesheets…' }));
    try {
      const [list, themes] = await Promise.all([api('GET', '/api/stylesheets'), themesList(), catalog()]);
      se.list = list; se.themes = themes.themes;
      const want = store.get('editor:stylesheet', '');
      const initial = list.stylesheets.find(s => s.file === want) || list.stylesheets.find(s => s.themes.includes(state.style)) || list.stylesheets[0];
      await loadSheet(initial.file);
    } catch (e) { fill(sheetsPanel, h('p', { class: 'edt-status bad', text: e.message })); return; }
  }
  drawSheets();
}

function previewThemes() {
  const info = se.list.stylesheets.find(s => s.file === (se.create ? se.from : se.file));
  const ids = info ? info.themes : [];
  const themes = se.themes.filter(t => ids.includes(t.id));
  return themes.length ? themes : se.themes;
}
function previewPalette() {
  const themes = previewThemes();
  const theme = themes.find(t => t.id === se.preview) || themes.find(t => t.id === state.style) || themes[0];
  se.preview = theme ? theme.id : '';
  const P = theme ? paletteOf(theme) : {};
  return { ...P, ...((theme && state.edits[theme.id]) || {}) };
}

function drawSheets() {
  const { bar, status } = editorBar('stylesheets', {
    onCheck: () => saveSheet(true, status),
    onSave: () => saveSheet(false, status),
    onChanges: () => sheetChanges(status),
    onDiscard: async () => { if (!confirmDiscard('stylesheets')) return; await loadSheet(se.create ? (se.from || se.list.stylesheets[0].file) : se.file); drawSheets(); },
  });
  se.statusEl = status;
  const picker = h('select', { class: 'edt', 'aria-label': 'Stylesheet', onchange: async e => {
    if (!confirmDiscard('stylesheets')) { e.target.value = se.file; return; }
    await loadSheet(e.target.value); drawSheets();
  } }, se.list.stylesheets.map(s => h('option', { value: s.file, text: s.file, selected: !se.create && s.file === se.file })),
  se.create ? h('option', { value: se.file, text: `New: ${se.file}`, selected: true }) : null);
  previewPalette();
  const preview = h('select', { class: 'edt', 'aria-label': 'Preview palette', onchange: e => { se.preview = e.target.value; drawSheets(); } },
    previewThemes().map(t => h('option', { value: t.id, text: t.name, selected: t.id === se.preview })));
  fill(sheetsPanel, 
    h('div', { class: 'edt-bar' }, h('label', {}, 'Stylesheet', picker), h('label', {}, 'Preview with', preview),
      h('button', { class: 'btn', type: 'button', text: 'New stylesheet…', onclick: newSheetDialog }),
      h('span', { class: 'grow' }), h('span', { class: 'edt-dirty', id: 'se-dirty' }), bar),
    status,
    h('div', { class: 'edt-layout wide-side' }, sheetTree(), h('div', { class: 'edt-main', id: 'se-main' }, entryEditor(), distinctEditor())));
  sheetChanged();
}

function sheetTree() {
  const side = h('nav', { class: 'edt-side', 'aria-label': 'Styles' });
  const search = h('input', { type: 'search', placeholder: 'Search styles…', value: se.search, 'aria-label': 'Search styles' });
  const list = h('div', { class: 'stack', style: { gap: '2px' } });
  const draw = () => {
    const q = se.search.toLowerCase();
    fill(list, );
    const groups = new Map();
    for (const e of se.entries) {
      if (q && !e.name.toLowerCase().includes(q)) continue;
      const parts = e.name.split('/');
      const group = parts.slice(0, Math.min(2, parts.length - 1)).join('/') || parts[0];
      if (!groups.has(group)) groups.set(group, []);
      groups.get(group).push(e);
    }
    for (const [group, entries] of groups) {
      const open = !!q || entries.some(e => e.name === se.selected) || store.get('se-open:' + group, false);
      const details = h('details', { open }, h('summary', { text: `${group} (${entries.length})${entries.some(entryDirty) ? ' •' : ''}` }));
      details.addEventListener('toggle', () => store.set('se-open:' + group, details.open));
      for (const e of entries) {
        add(details, h('button', {
          class: 'entry' + (e.name === se.selected ? ' active' : ''), type: 'button', onclick: () => { se.selected = e.name; store.set('editor:style', e.name); drawSheets(); },
        }, h('span', { class: e.deleted ? 'deleted' : '', text: e.name.slice(group.length + 1) || e.name }), h('span', { class: 'n', text: entryDirty(e) ? '•' : '' })));
      }
      add(list, details);
    }
    if (!groups.size) add(list, h('p', { class: 'empty', text: 'No styles match.' }));
  };
  search.addEventListener('input', () => { se.search = search.value; draw(); });
  draw();
  add(side, search, h('button', { class: 'btn small', type: 'button', text: 'New style…', onclick: () => newStyleDialog() }), list);
  return side;
}

// resolveStyle mirrors the compiler: combined styles in order, later ones
// winning, then the style's own properties.
function colourValue(v, P) { return v.startsWith('#') ? { hex: v } : { var: v, hex: P[v] || '#FF00FF' }; }
function ownChannels(props, P) {
  const own = {};
  if (props.TextColor) own.text = colourValue(props.TextColor, P);
  if (props.BackgroundColor) own.background = colourValue(props.BackgroundColor, P);
  if (props.BorderColor) own.border = colourValue(props.BorderColor, P);
  if (props.FontSize) own.fontSize = props.FontSize;
  if (props.Beam) { if (props.Beam.Color) own.beam = props.Beam.Color; if (props.Beam.Temp !== undefined && props.Beam.Temp !== null) own.beamTemp = props.Beam.Temp; }
  if (props.Minimap) {
    if (props.Minimap.Shape) own.minimapShape = props.Minimap.Shape;
    if (props.Minimap.Color) own.minimapColor = props.Minimap.Color;
    if (props.Minimap.Size !== undefined && props.Minimap.Size !== null) own.minimapSize = props.Minimap.Size;
  }
  if (props.DropSound) own.sound = props.DropSound;
  if (props.DropVolume) own.volume = props.DropVolume;
  return own;
}
function resolveStyle(name, P, visiting = new Set(), skipOwn = false) {
  const entry = se.byName.get(name);
  if (!entry || entry.deleted) return { look: {}, from: {}, problems: [`"${name}" does not exist`] };
  if (visiting.has(name)) return { look: {}, from: {}, problems: [`"${name}" combines itself`] };
  visiting.add(name);
  const look = {}, from = {}, problems = [];
  for (const dep of entry.props.Combination || []) {
    const r = resolveStyle(dep, P, visiting);
    Object.assign(look, r.look); Object.assign(from, r.from); problems.push(...r.problems);
  }
  visiting.delete(name);
  if (!skipOwn) for (const [k, v] of Object.entries(ownChannels(entry.props, P))) { look[k] = v; from[k] = name; }
  return { look, from, problems };
}

function entryEditor() {
  const e = se.byName.get(se.selected);
  if (!e) return h('div', { class: 'edt-card' }, h('p', { class: 'hint', text: 'Pick a style on the left, or make a new one.' }));
  const P = previewPalette();
  const leaf = e.name.split('/').pop();
  const previewBox = h('div', { class: 'edt-preview' });
  const factsBox = h('div');
  const refresh = () => {
    const r = resolveStyle(e.name, P);
    fill(previewBox, labelEl(leaf, r.look, P, 'Normal', 1.1));
    const lc = r.look.text ? lookLc(r.look, P) : null;
    const target = textTarget(r.look.fontSize || 32);
    fill(factsBox, styleFacts(r.look, P),
      h('p', { class: 'hint', text: lc === null ? 'No text colour: the game uses its default for the rarity.' : `Text contrast Lc ${lc.toFixed(0)} (needs ${target} at ${r.look.fontSize || 32} pt)${lc < target ? ' — too low' : ''}` }),
      r.problems.length ? h('p', { class: 'edt-status bad', text: r.problems.join('\n') }) : null);
    sheetChanged();
  };
  const changed = () => { refresh(); };
  const card = h('div', { class: 'edt-card' },
    h('div', { class: 'edt-row' }, h('h3', { text: e.name }), e.isNew ? h('span', { class: 'pill ok', text: 'new' }) : null, e.deleted ? h('span', { class: 'pill bad', text: 'deleted' }) : null,
      tools(
        h('button', { class: 'btn small', type: 'button', text: 'Duplicate…', onclick: () => newStyleDialog(e) }),
        e.deleted
          ? h('button', { class: 'btn small', type: 'button', text: 'Restore', onclick: () => { e.deleted = false; drawSheets(); } })
          : h('button', { class: 'btn small danger', type: 'button', text: 'Delete', onclick: () => {
            const users = se.entries.filter(x => !x.deleted && (x.props.Combination || []).includes(e.name)).map(x => x.name);
            if (users.length && !confirm(`${users.length} style${users.length === 1 ? '' : 's'} combine ${e.name} (${users.slice(0, 5).join(', ')}${users.length > 5 ? ', …' : ''}). Delete it anyway? Saving will fail until they no longer use it.`)) return;
            if (e.isNew) { se.entries = se.entries.filter(x => x !== e); se.byName.delete(e.name); se.selected = null; } else e.deleted = true;
            drawSheets();
          } }))),
    (e.comments || []).length ? h('pre', { class: 'hint', style: { margin: 0, whiteSpace: 'pre-wrap', fontFamily: 'var(--mono)', fontSize: '.75rem' }, text: e.comments.join('\n') }) : null,
    previewBox, factsBox,
    combinationEditor(e, changed),
    propsEditor(e, P, changed),
    e.props.other ? h('div', { class: 'stack', style: { gap: '4px' } }, h('p', { class: 'hint', text: 'Other keys, kept as they are:' }),
      h('div', { class: 'edt-chips' }, Object.entries(e.props.other).map(([k, v]) => h('span', { text: `${k} = ${v}` })))) : null);
  refresh();
  return card;
}

function combinationEditor(e, changed) {
  const box = h('div', { class: 'stack', style: { gap: '6px' } });
  const names = se.entries.filter(x => !x.deleted && x.name !== e.name).map(x => x.name);
  datalist('dl-sheet-styles', names);
  const draw = () => {
    const list = e.props.Combination || [];
    fill(box, h('div', { class: 'edt-row' }, h('strong', { text: 'Combines' }), h('span', { class: 'hint', text: 'in order; later styles win, and the style\'s own properties win over all of them' })));
    const rows = h('div', { class: 'edt-list' });
    list.forEach((name, i) => {
      const input = h('input', { class: 'edt mono fill', value: name, 'aria-label': 'Combined style' });
      input.setAttribute('list', 'dl-sheet-styles');
      const check = () => input.classList.toggle('invalid', !se.byName.has(input.value) || !!se.byName.get(input.value).deleted);
      input.addEventListener('change', () => { list[i] = input.value.trim(); check(); changed(); });
      check();
      const move = d => { const j = i + d; if (j < 0 || j >= list.length) return; [list[i], list[j]] = [list[j], list[i]]; changed(); draw(); };
      add(rows, h('div', { class: 'edt-row' }, h('span', { class: 'mono muted', text: String(i + 1) }), input,
        tools(iconButton('↑', 'Earlier (loses to later styles)', () => move(-1), i === 0), iconButton('↓', 'Later (wins over earlier styles)', () => move(1), i === list.length - 1),
          iconButton('×', 'Remove', () => { list.splice(i, 1); if (!list.length) delete e.props.Combination; changed(); draw(); }))));
    });
    add(box, rows, h('div', {}, h('button', { class: 'btn small', type: 'button', text: '+ combine a style', onclick: () => { e.props.Combination = [...list, '']; changed(); draw(); } })));
  };
  draw();
  return box;
}

function propsEditor(e, P, changed) {
  const grid = h('div', { class: 'edt-props' });
  const paletteNames = Object.keys(P).sort();
  datalist('dl-palette', paletteNames);
  catalog().then(c => datalist('dl-sounds', c.sounds || []));
  const inherited = resolveStyle(e.name, P, new Set(), true);
  const describe = key => {
    const l = inherited.look;
    switch (key) {
      case 'TextColor': return l.text ? (l.text.var || l.text.hex) : '';
      case 'BorderColor': return l.border ? (l.border.var || l.border.hex) : '';
      case 'BackgroundColor': return l.background ? (l.background.var || l.background.hex) : '';
      case 'FontSize': return l.fontSize ? `${l.fontSize} pt` : '';
      case 'Minimap': return l.minimapShape ? `${l.minimapShape} ${l.minimapColor || ''} ${l.minimapSize ?? ''}` : '';
      case 'Beam': return l.beam ? `${l.beam}${l.beamTemp ? ' (temp)' : ''}` : '';
      case 'DropSound': return l.sound || '';
      case 'DropVolume': return l.volume ? String(l.volume) : '';
    }
    return '';
  };
  const channelOf = { TextColor: 'text', BorderColor: 'border', BackgroundColor: 'background', FontSize: 'fontSize', Minimap: 'minimapShape', Beam: 'beam', DropSound: 'sound', DropVolume: 'volume' };
  const row = (key, label, control, defaultValue) => {
    const own = e.props[key] !== undefined;
    const box = h('div', { class: 'edt-row' });
    const toggle = h('input', { type: 'checkbox', checked: own, title: 'Set here instead of inheriting' });
    const draw = () => {
      fill(box, );
      if (e.props[key] !== undefined) add(box, control());
      else {
        const from = inherited.from[channelOf[key]];
        add(box, h('span', { class: 'inherited', text: describe(key) ? `${describe(key)} from ${from}` : 'not set' }));
      }
    };
    toggle.addEventListener('change', () => {
      if (toggle.checked) e.props[key] = clone(defaultValue()); else delete e.props[key];
      draw(); changed();
    });
    draw();
    add(grid, toggle, h('span', { text: label }), box);
  };
  const colourControl = key => () => {
    const input = h('input', { class: 'edt mono', value: e.props[key], 'aria-label': key });
    input.setAttribute('list', 'dl-palette');
    const swatch = h('span', { class: 'style-swatch', style: { width: '18px', height: '18px' } });
    const check = () => {
      const v = input.value.trim();
      const ok = HEX.test(v) || v in P;
      input.classList.toggle('invalid', !ok);
      input.title = ok ? '' : 'Use a palette colour or #RRGGBB(AA)';
      swatch.style.background = HEX.test(v) ? v : (P[v] || 'transparent');
    };
    input.addEventListener('input', () => { e.props[key] = input.value.trim(); check(); changed(); });
    check();
    return h('span', { class: 'edt-row' }, swatch, input);
  };
  const colourDefault = key => () => { const l = inherited.look[{ TextColor: 'text', BorderColor: 'border', BackgroundColor: 'background' }[key]]; return l ? (l.var || l.hex) : paletteNames[0] || '#FFFFFF'; };
  row('TextColor', 'Text', colourControl('TextColor'), colourDefault('TextColor'));
  row('BackgroundColor', 'Background', colourControl('BackgroundColor'), colourDefault('BackgroundColor'));
  row('BorderColor', 'Border', colourControl('BorderColor'), colourDefault('BorderColor'));
  row('FontSize', 'Font size', () => h('input', { class: 'edt', type: 'number', min: 1, max: 45, value: e.props.FontSize, style: { width: '80px' }, oninput: ev => { e.props.FontSize = Number(ev.target.value) || 1; changed(); } }), () => inherited.look.fontSize || 32);
  const optionSelect = (values, current, onchange, blank) => h('select', { class: 'edt', onchange: ev => onchange(ev.target.value) },
    blank !== undefined ? h('option', { value: '', text: blank, selected: current === undefined || current === null }) : null,
    values.map(v => h('option', { value: String(v), text: String(v), selected: String(v) === String(current) })));
  catalog().then(c => { se.shapes = c.minimapShapes; se.colours = c.colours; });
  const shapes = se.shapes || ['Circle', 'Diamond', 'Hexagon', 'Square', 'Star', 'Triangle', 'Cross', 'Moon', 'Raindrop', 'Kite', 'Pentagon', 'UpsideDownHouse'];
  const colours = se.colours || ['Red', 'Green', 'Blue', 'Brown', 'White', 'Yellow', 'Cyan', 'Grey', 'Orange', 'Pink', 'Purple'];
  row('Minimap', 'Minimap icon', () => h('span', { class: 'edt-row' },
    optionSelect(shapes, e.props.Minimap.Shape, v => { e.props.Minimap.Shape = v || undefined; changed(); }, 'inherit shape'),
    optionSelect(colours, e.props.Minimap.Color, v => { e.props.Minimap.Color = v || undefined; changed(); }, 'inherit colour'),
    optionSelect([0, 1, 2], e.props.Minimap.Size, v => { e.props.Minimap.Size = v === '' ? undefined : Number(v); changed(); }, 'inherit size')),
  () => ({ Shape: inherited.look.minimapShape || 'Circle' }));
  row('Beam', 'Beam', () => h('span', { class: 'edt-row' },
    optionSelect(colours, e.props.Beam.Color, v => { e.props.Beam.Color = v || undefined; changed(); }, 'inherit colour'),
    optionSelect(['temporary', 'permanent'], e.props.Beam.Temp === undefined ? undefined : e.props.Beam.Temp ? 'temporary' : 'permanent', v => { e.props.Beam.Temp = v === '' ? undefined : v === 'temporary'; changed(); }, 'inherit duration')),
  () => ({ Color: inherited.look.beam || 'White', Temp: !!inherited.look.beamTemp }));
  row('DropSound', 'Sound', () => { const i = h('input', { class: 'edt mono', value: e.props.DropSound, oninput: ev => { e.props.DropSound = ev.target.value.trim(); changed(); } }); i.setAttribute('list', 'dl-sounds'); return i; }, () => inherited.look.sound || 'click.mp3');
  row('DropVolume', 'Volume', () => h('input', { class: 'edt', type: 'number', min: 0, max: 300, value: e.props.DropVolume, style: { width: '80px' }, oninput: ev => { e.props.DropVolume = Math.max(0, Math.min(300, Number(ev.target.value) || 0)); changed(); } }), () => inherited.look.volume || 150);
  row('Comment', 'Comment', () => h('input', { class: 'edt fill', value: e.props.Comment, oninput: ev => { e.props.Comment = ev.target.value; changed(); } }), () => '');
  return h('div', { class: 'stack', style: { gap: '6px' } }, h('div', { class: 'edt-row' }, h('strong', { text: 'Own properties' }), h('span', { class: 'hint', text: 'tick a property to set it here; otherwise it comes from the combined styles' })), grid);
}

function distinctEditor() {
  const P = previewPalette();
  datalist('dl-palette', Object.keys(P).sort());
  const details = h('details', { class: 'pv-panel' }, h('summary', {}, h('span', { text: 'Distinct colour groups' }), h('span', { class: 'pv-sum', text: `${se.distinct.length} groups` })));
  const body = h('div', { class: 'stack', style: { gap: '8px' } }, h('p', { class: 'hint', text: 'The palette view checks that the colours in each group stay at least Min apart (CIEDE2000).' }));
  se.distinct.forEach((g, i) => {
    const vars = h('input', { class: 'edt mono fill', value: g.vars.join(', '), 'aria-label': `${g.name} colours` });
    vars.addEventListener('input', () => { g.vars = vars.value.split(',').map(s => s.trim()).filter(Boolean); vars.classList.toggle('invalid', g.vars.some(v => !(v in P))); sheetChanged(); });
    add(body, h('div', { class: 'edt-row' },
      h('input', { class: 'edt', value: g.name, style: { width: '170px' }, 'aria-label': 'Group name', oninput: ev => { g.name = ev.target.value.trim(); sheetChanged(); } }),
      h('label', {}, 'Min', h('input', { class: 'edt', type: 'number', min: 0, step: 0.5, value: g.min, style: { width: '70px' }, oninput: ev => { g.min = Number(ev.target.value) || 0; sheetChanged(); } })),
      vars, iconButton('×', 'Remove the group', () => { se.distinct.splice(i, 1); sheetChanged(); drawSheets(); })));
  });
  add(body, h('div', {}, h('button', { class: 'btn small', type: 'button', text: 'Add group', onclick: () => { se.distinct.push({ name: 'NewGroup', min: 10, vars: [] }); sheetChanged(); drawSheets(); } })));
  add(details, body);
  return details;
}

function newStyleDialog(copyOf) {
  const name = h('input', { class: 'edt mono', value: copyOf ? copyOf.name + '-copy' : (se.selected ? se.selected.split('/').slice(0, -1).concat('New').join('/') : 'Final/New') });
  const problem = h('p', { class: 'edt-status bad' });
  const dialog = showDialog(copyOf ? `Duplicate ${copyOf.name}` : 'New style', h('div', { class: 'stack', style: { gap: '8px' } },
    field('Style path', name, 'Parts separated by /, such as Final/Tattoos/T7'), problem), [
    h('button', { class: 'btn primary', type: 'button', text: 'Create', onclick: () => {
      const n = name.value.trim();
      if (!STYLE_NAME.test(n)) { problem.textContent = 'Use letters, digits, _ and - in parts separated by /.'; return; }
      if (se.byName.has(n) && !se.byName.get(n).deleted) { problem.textContent = `${n} already exists.`; return; }
      const entry = { name: n, comments: [], props: copyOf ? clone(copyOf.props) : { Combination: [] }, isNew: true };
      if (!copyOf) delete entry.props.Combination;
      se.entries.push(entry); se.byName.set(n, entry); se.selected = n;
      dialog.close(); drawSheets();
    } }),
  ]);
  name.focus();
}

function newSheetDialog() {
  if (!confirmDiscard('stylesheets')) return;
  const file = h('input', { class: 'edt mono', placeholder: 'my-theme.toml' });
  const from = h('select', { class: 'edt' }, h('option', { value: '', text: 'Empty' }), se.list.stylesheets.map(s => h('option', { value: s.file, text: s.file, selected: s.file === se.file })));
  const problem = h('p', { class: 'edt-status bad' });
  const dialog = showDialog('New stylesheet', h('div', { class: 'edt-grid' }, field('File name', file), field('Start as a copy of', from), problem), [
    h('button', { class: 'btn primary', type: 'button', text: 'Create', onclick: async () => {
      let f = file.value.trim();
      if (f && !f.endsWith('.toml')) f += '.toml';
      if (!/^[A-Za-z0-9][A-Za-z0-9._ -]*\.toml$/.test(f)) { problem.textContent = 'Give the stylesheet a file name (letters, digits, spaces, . _ -).'; return; }
      if (se.list.stylesheets.some(s => s.file === f)) { problem.textContent = `${f} already exists.`; return; }
      await loadSheet(f, { create: true, from: from.value });
      dialog.close(); drawSheets();
      setStatus(se.statusEl, 'Save to create the file. A theme can use it once it exists.', 'warn');
    } }),
  ]);
  file.focus();
}

function sheetRequest(dryRun) {
  const changes = [];
  for (const e of se.entries) {
    if (!entryDirty(e)) continue;
    if (e.deleted) { if (!e.isNew) changes.push({ name: e.name, delete: true }); continue; }
    changes.push({ name: e.name, props: cleanProps(e.props) });
  }
  const original = JSON.parse(se.distinctOriginal || '[]');
  const distinct = [];
  if (JSON.stringify(se.distinct) !== se.distinctOriginal) {
    se.distinct.forEach(g => distinct.push({ name: g.name, min: g.min, vars: g.vars }));
    original.forEach(g => { if (!se.distinct.some(x => x.name === g.name)) distinct.push({ name: g.name, min: 0, vars: [], delete: true }); });
  }
  return { hash: se.hash, changes, distinct, create: se.create, from: se.from, dryRun };
}
function sheetProblems() {
  const problems = [];
  for (const e of se.entries) {
    if (e.deleted) continue;
    (e.props.Combination || []).forEach(dep => { if (!se.byName.has(dep) || se.byName.get(dep).deleted) problems.push(`${e.name} combines "${dep}", which does not exist.`); });
  }
  se.distinct.forEach(g => { if (!/^[A-Za-z0-9_-]+$/.test(g.name)) problems.push(`"${g.name}" is not a valid group name.`); });
  return problems;
}
async function saveSheet(dryRun, status) {
  const problems = sheetProblems();
  if (problems.length) { setStatus(status, problems.slice(0, 12).join('\n'), 'bad'); return; }
  if (!dryRun && !mayReload('stylesheets')) return;
  setStatus(status, dryRun ? 'Checking: compiling every script with the themes that use this stylesheet…' : 'Saving: checking, writing and recompiling every filter…');
  try {
    const result = await api('PUT', `/api/stylesheets/${encodeURIComponent(se.file)}`, sheetRequest(dryRun));
    if (dryRun) { setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok'); return; }
    store.set('editor:stylesheet', se.file);
    reloadInto('stylesheets');
  } catch (e) { setStatus(status, e.message, 'bad'); }
}
async function sheetChanges(status) {
  const problems = sheetProblems();
  if (problems.length) { setStatus(status, problems.slice(0, 12).join('\n'), 'bad'); return; }
  setStatus(status, 'Preparing the file…');
  try {
    const result = await api('PUT', `/api/stylesheets/${encodeURIComponent(se.file)}`, sheetRequest(true));
    setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok');
    showDialog(`Changes to ${se.file}`, diffView(result.original, result.contents));
  } catch (e) { setStatus(status, e.message, 'bad'); }
}

/* ---------- scripts ---------- */
const scriptsPanel = addView({ id: 'scripts', label: 'Scripts', render: () => renderScripts() });
const sc = { files: null, file: null, doc: null, hash: '', original: '', create: false, parent: new WeakMap(), catalog: null, open: new Set(store.get('sc-open', [])) };

function scriptDirty() { return sc.doc && (sc.create || JSON.stringify(sc.doc) !== sc.original); }
function scriptChanged() {
  if (scriptDirty()) dirtyEditors.add('scripts'); else dirtyEditors.delete('scripts');
  const flag = document.getElementById('sc-dirty');
  if (flag) flag.textContent = scriptDirty() ? 'Unsaved changes' : '';
}
const CONTAINERS = new Set(['metadata', 'section', 'conditions', 'rules', 'macro']);
function indexParents(items, parent) {
  for (const item of items || []) {
    // The server leaves out empty lists.
    if (CONTAINERS.has(item.kind) && !item.items) item.items = [];
    sc.parent.set(item, parent); indexParents(item.items, item);
  }
}
// touch marks an item changed: it and every item around it are written anew.
function touch(item) {
  for (let x = item; x; x = sc.parent.get(x)) {
    if (!(x === item && x.kind === 'source')) { delete x.raw; delete x.indent; delete x.closeIndent; }
  }
  scriptChanged();
}
// listChanged marks a container whose list of items changed.
function listChanged(container) { if (container) touch(container); else scriptChanged(); }
function adopt(item, parent) { sc.parent.set(item, parent); indexParents(item.items, item); return item; }
function stripRaw(item) {
  const copy = clone(item);
  const walk = x => { if (x.kind !== 'source') { delete x.raw; delete x.indent; delete x.closeIndent; } (x.items || []).forEach(walk); };
  walk(copy);
  return copy;
}

async function loadScript(file, template) {
  sc.create = !!template;
  if (template) {
    sc.file = file; sc.doc = template; sc.hash = ''; sc.source = '';
  } else {
    const r = await api('GET', `/api/scripts/${encodeURIComponent(file)}`);
    sc.file = r.file; sc.doc = r.document; sc.hash = r.hash;
    store.set('editor:script', file);
  }
  sc.parent = new WeakMap();
  sc.doc.items = sc.doc.items || [];
  indexParents(sc.doc.items, null);
  sc.original = template ? '' : JSON.stringify(sc.doc);
  scriptChanged();
}

async function renderScripts() {
  if (!sc.files) {
    fill(scriptsPanel, h('p', { class: 'muted', text: 'Loading scripts…' }));
    try {
      const [list, cat] = await Promise.all([api('GET', '/api/scripts'), catalog()]);
      sc.files = list.scripts; sc.catalog = cat;
      sc.macros = new Map(cat.macros.map(m => [m.name, m]));
      sc.conditions = new Map(cat.conditions.map(c => [c.id, c]));
      datalist('dl-basetypes', cat.baseTypes);
      datalist('dl-classes', cat.itemClasses);
      datalist('dl-categories', cat.categories);
      const want = store.get('editor:script', '');
      const initial = sc.files.includes(want) ? want : (sc.files.find(f => DATA.filters.some(x => x.scriptFile === f && x.script === state.script)) || sc.files[0]);
      if (initial) await loadScript(initial);
    } catch (e) { fill(scriptsPanel, h('p', { class: 'edt-status bad', text: e.message })); return; }
  }
  drawScripts();
}

function scriptVars() {
  const names = [];
  (sc.doc ? sc.doc.items : []).forEach(item => { if (item.kind === 'var') item.assignments.forEach(a => names.push('$' + a.name)); });
  return names;
}
function refreshStyleList() { datalist('dl-script-styles', [...scriptVars(), ...sc.catalog.styles, ...sc.catalog.partialStyles]); }

function drawScripts() {
  const { bar, status } = editorBar('scripts', {
    onCheck: () => saveScript(true, status),
    onSave: () => saveScript(false, status),
    onChanges: () => scriptChanges(status),
    onDiscard: async () => { if (!confirmDiscard('scripts')) return; await loadScript(sc.create ? sc.files[0] : sc.file); drawScripts(); },
  });
  sc.statusEl = status;
  if (!sc.doc) {
    fill(scriptsPanel, h('div', { class: 'edt-bar' }, h('button', { class: 'btn', type: 'button', text: 'New script…', onclick: newScriptDialog })), h('p', { class: 'hint', text: 'There are no scripts yet.' }));
    return;
  }
  refreshStyleList();
  const picker = h('select', { class: 'edt', 'aria-label': 'Script', onchange: async e => {
    if (!confirmDiscard('scripts')) { e.target.value = sc.file; return; }
    await loadScript(e.target.value); drawScripts();
  } }, sc.files.map(f => h('option', { value: f, text: f, selected: !sc.create && f === sc.file })), sc.create ? h('option', { value: sc.file, text: `New: ${sc.file}`, selected: true }) : null);
  const main = h('div', { class: 'edt-main', id: 'sc-main' });
  drawItems(main, sc.doc.items, null, 'top');
  fill(scriptsPanel, 
    h('div', { class: 'edt-bar' }, h('label', {}, 'Script', picker), h('button', { class: 'btn', type: 'button', text: 'New script…', onclick: newScriptDialog }),
      h('span', { class: 'grow' }), h('span', { class: 'edt-dirty', id: 'sc-dirty' }), bar),
    status,
    h('div', { class: 'edt-layout' }, scriptOutline(), main));
  scriptChanged();
}

function sectionName(section) {
  const meta = (section.items || []).find(i => i.kind === 'metadata');
  const name = meta && meta.items.find(f => f.kind === 'field' && f.key === 'NAME');
  return name && name.value ? name.value.text : 'Section';
}
function scriptOutline() {
  const nav = h('nav', { class: 'edt-side', 'aria-label': 'Script outline' });
  sc.doc.items.forEach((item, i) => {
    let label = null;
    if (item.kind === 'metadata') label = 'Script settings';
    else if (item.kind === 'section') label = sectionName(item);
    else if (item.kind === 'var' && !sc.doc.items.slice(0, i).some(x => x.kind === 'var')) label = 'Variables';
    else if (item.kind === 'import') label = 'Import';
    if (!label) return;
    const rules = item.kind === 'section' ? countRules(item) : null;
    add(nav, h('button', { class: 'entry', type: 'button', onclick: () => {
      const target = document.getElementById('sc-item-' + i);
      if (!target) return;
      if (target.tagName === 'DETAILS' && !target.open) target.open = true;
      target.scrollIntoView({ behavior: 'smooth', block: 'start' });
    } }, h('span', { text: label }), h('span', { class: 'n', text: rules === null ? '' : String(rules) })));
  });
  return nav;
}
function countRules(section) {
  const rules = (section.items || []).find(i => i.kind === 'rules');
  return rules ? rules.items.filter(i => i.kind === 'rule' || i.kind === 'macro').length : 0;
}

// Adding items. Each context offers what the language allows there.
const ADDABLE = {
  top: [['section', 'Section'], ['var', 'Variable'], ['import', 'Import'], ['comment', 'Comment'], ['blank', 'Empty line']],
  rules: [['rule', 'Rule'], ['macro', 'Macro'], ['comment', 'Comment'], ['blank', 'Empty line']],
  conditions: [['where', 'Condition line'], ['comment', 'Comment'], ['blank', 'Empty line']],
  params: [['param', 'Parameter'], ['comment', 'Comment']],
  section: [['comment', 'Comment'], ['blank', 'Empty line']],
  metadata: [['comment', 'Comment']],
};
function newCondition(id = '@rarity') {
  const spec = sc.conditions.get(id) || { kind: 'choice', quoted: true, choices: ['Rare'] };
  const text = spec.kind === 'choice' ? spec.choices[0] : spec.kind === 'number' ? '1' : '';
  return { id, op: '==', value: { text, kind: spec.quoted ? 'string' : 'number' } };
}
function newItem(kind) {
  const firstStyle = (sc.catalog.styles.find(s => s.startsWith('Final/')) || sc.catalog.styles[0] || '');
  switch (kind) {
    case 'section': return { kind: 'section', items: [
      { kind: 'metadata', items: [{ kind: 'field', key: 'NAME', value: { text: 'New section', kind: 'string' } }, { kind: 'field', key: 'DESCRIPTION', value: { text: 'What this section shows', kind: 'string' } }] },
      { kind: 'blank', count: 1 }, { kind: 'conditions', items: [] }, { kind: 'blank', count: 1 }, { kind: 'rules', items: [] }] };
    case 'var': { let n = 1; while (scriptVars().includes('$style_' + n)) n++; return { kind: 'var', assignments: [{ name: 'style_' + n, values: [{ text: firstStyle, kind: 'string' }] }] }; }
    case 'import': return { kind: 'import', value: { text: (sc.files.find(f => f !== sc.file) || 'other.rf'), kind: 'string' } };
    case 'comment': return { kind: 'comment', text: '!! ' };
    case 'blank': return { kind: 'blank', count: 1 };
    case 'where': return { kind: 'where', conditions: [newCondition('@area_level')].map(c => ({ ...c, op: '<', value: { text: '68', kind: 'number' } })) };
    case 'rule': return { kind: 'rule', conditions: [newCondition()], style: { text: firstStyle, kind: 'string' }, action: { text: '$Show', kind: 'var' } };
    case 'macro': return macroWithDefaults('handle_csv');
    case 'param': return { kind: 'param', key: '$style', value: { text: firstStyle, kind: 'string' } };
  }
  return null;
}
function macroWithDefaults(name) {
  const spec = sc.macros.get(name);
  const macro = { kind: 'macro', name, items: [] };
  if (spec && spec.tiers) {
    const firstStyle = sc.catalog.styles.find(s => s.startsWith('Final/')) || '';
    macro.items.push({ kind: 'param', key: '$tier_1', value: { text: firstStyle, kind: 'string' } });
  }
  (spec ? spec.params : []).filter(p => p.required).forEach(p => macro.items.push({ kind: 'param', key: p.key, value: defaultParamValue(p) }));
  return macro;
}
function defaultParamValue(param) {
  if (param && param.value === 'category') return { text: sc.catalog.categories[0] || '', kind: 'string' };
  return { text: '', kind: 'string' };
}

function addMenu(context, onAdd) {
  const select = h('select', { class: 'edt-add', title: 'Add after this', 'aria-label': 'Add after this' },
    h('option', { value: '', text: '+ add' }), (ADDABLE[context] || []).map(([k, label]) => h('option', { value: k, text: label })));
  select.addEventListener('change', () => { const kind = select.value; select.value = ''; if (kind) onAdd(kind); });
  return select;
}
function itemTools(list, index, container, context, redraw) {
  const item = list[index];
  const move = d => {
    const j = index + d;
    if (j < 0 || j >= list.length) return;
    [list[index], list[j]] = [list[j], list[index]];
    listChanged(container); redraw();
  };
  return tools(
    iconButton('↑', 'Move up', () => move(-1), index === 0),
    iconButton('↓', 'Move down', () => move(1), index === list.length - 1),
    iconButton('⧉', 'Duplicate', () => { list.splice(index + 1, 0, adopt(stripRaw(item), container)); listChanged(container); redraw(); }),
    iconButton('×', 'Delete', () => {
      if (item.kind === 'section' && !confirm(`Delete the section "${sectionName(item)}" and its ${countRules(item)} rules?`)) return;
      list.splice(index, 1); listChanged(container); redraw();
    }),
    addMenu(context, kind => { list.splice(index + 1, 0, adopt(newItem(kind), container)); listChanged(container); redraw(); }));
}

// drawItems renders a list of items in order, so comments stay where they are.
function drawItems(host, list, container, context) {
  const redraw = () => { fill(host, ); drawItems(host, list, container, context); if (context === 'top') refreshOutline(); };
  list.forEach((item, index) => add(host, itemView(item, list, index, container, context, redraw)));
  if (!list.length || context !== 'top') {
    add(host, h('div', { class: 'edt-row' }, addMenu(context, kind => { list.push(adopt(newItem(kind), container)); listChanged(container); redraw(); })));
  } else {
    add(host, h('div', { class: 'edt-row' }, h('button', { class: 'btn small', type: 'button', text: '+ section at the end', onclick: () => { list.push(adopt(newItem('section'), container)); listChanged(container); redraw(); } })));
  }
}
function refreshOutline() {
  const old = scriptsPanel.querySelector('.edt-side');
  if (old) old.replaceWith(scriptOutline());
}

function itemView(item, list, index, container, context, redraw) {
  const toolbar = () => itemTools(list, index, container, context, redraw);
  const trailing = item.trailing && item.trailing.trim() ? h('span', { class: 'trailing', title: 'Comment after the item, kept as it is', text: item.trailing.trim() }) : null;
  switch (item.kind) {
    case 'blank':
      return h('div', { class: 'edt-blank' }, iconButton('×', 'Remove the empty line', () => { list.splice(index, 1); listChanged(container); redraw(); }));
    case 'comment': {
      const block = item.text.startsWith('!![');
      const input = block
        ? h('textarea', { class: 'edt edt-comment', rows: Math.min(8, item.text.split('\n').length), value: item.text })
        : h('input', { class: 'edt edt-comment', value: item.text, 'aria-label': 'Comment' });
      input.addEventListener('input', () => {
        const ok = block ? input.value.startsWith('!![') && input.value.endsWith(']!!') : input.value.startsWith('!!') && !input.value.includes('\n');
        input.classList.toggle('invalid', !ok);
        input.title = ok ? '' : block ? 'A block comment starts with !![ and ends with ]!!' : 'A comment starts with !!';
        item.text = input.value; touch(item);
      });
      return h('div', { class: 'edt-item' }, input, trailing, toolbar());
    }
    case 'source': {
      const area = h('textarea', { class: 'edt', rows: Math.min(20, item.raw.split('\n').length + 1), value: item.raw, spellcheck: false });
      area.addEventListener('input', () => { item.raw = area.value; touch(item); });
      return h('div', { class: 'edt-item macro', id: context === 'top' ? 'sc-item-' + index : undefined },
        h('div', { class: 'edt-row' }, h('span', { class: 'kw', text: 'SOURCE' }), h('span', { class: 'hint', text: 'This block\'s layout does not fit the form (for example, several rules on one line), so it is edited as text. Checking and saving read it like any script.' }), toolbar()), area);
    }
    case 'metadata': return metadataView(item, context === 'top', index, toolbar);
    case 'import': {
      const input = h('input', { class: 'edt mono', value: item.value.text, 'aria-label': 'Imported script' });
      input.setAttribute('list', 'dl-script-files');
      datalist('dl-script-files', sc.files);
      input.addEventListener('input', () => { item.value = { text: input.value.trim(), kind: 'string' }; touch(item); });
      return h('div', { class: 'edt-item', id: 'sc-item-' + index }, h('span', { class: 'kw', text: 'IMPORT' }), input, trailing, toolbar());
    }
    case 'var': return varView(item, index, toolbar, trailing);
    case 'section': return sectionView(item, index, toolbar);
    case 'conditions': return blockView(item, 'Only for items where', 'conditions', 'Every rule of the section also needs these conditions. A rule\'s own condition on the same property with the same comparison replaces the section\'s.', trailing);
    case 'rules': return blockView(item, 'Rules', 'rules', 'The first rule an item matches decides how it looks.', trailing);
    case 'where': return h('div', { class: 'edt-item' }, conditionsEditor(item), trailing, toolbar());
    case 'rule': return ruleView(item, toolbar, trailing);
    case 'macro': return macroView(item, toolbar, trailing);
  }
  return h('div', { class: 'edt-item' }, h('span', { class: 'hint', text: `(${item.kind})` }));
}

function fieldControl(fieldItem, touchItem) {
  const key = fieldItem.key;
  if (key === 'STRICTNESS') {
    return h('select', { class: 'edt', onchange: e => { fieldItem.value = { text: e.target.value, kind: 'keyword' }; touchItem(); } },
      sc.catalog.strictnesses.map(s => h('option', { value: s, text: s, selected: s === fieldItem.value.text })));
  }
  if (key === 'BUILD') {
    const options = [...sc.catalog.presets.map(p => ['string', p]), ...sc.catalog.builds.map(b => ['keyword', b])];
    if (!options.some(([k, v]) => v === fieldItem.value.text)) options.unshift([fieldItem.value.kind, fieldItem.value.text]);
    return h('select', { class: 'edt', onchange: e => { const [kind, text] = JSON.parse(e.target.value); fieldItem.value = { text, kind }; touchItem(); } },
      options.map(([kind, text]) => h('option', { value: JSON.stringify([kind, text]), text: kind === 'string' ? `${text} (equipment preset)` : text, selected: text === fieldItem.value.text })));
  }
  const input = h('input', { class: 'edt', value: fieldItem.value.text, 'aria-label': key });
  input.addEventListener('input', () => { fieldItem.value = { text: input.value, kind: 'string' }; touchItem(); input.classList.toggle('invalid', /[^\p{L}\p{N} \t._[\]\-/']/u.test(input.value)); });
  return input;
}
function metadataView(item, top, index, toolbar) {
  const grid = h('div', { class: 'edt-grid' });
  const fields = item.items.filter(i => i.kind === 'field');
  fields.forEach(f => {
    const removable = (!top && f.key === 'STRICTNESS') || (top && f.key === 'THEMES');
    add(grid, field(f.key, h('span', { class: 'edt-row' }, fieldControl(f, () => touch(f)),
      removable ? iconButton('×', 'Remove', () => { item.items.splice(item.items.indexOf(f), 1); listChanged(item); drawScripts(); }) : null)));
  });
  if (!top && !fields.some(f => f.key === 'STRICTNESS')) {
    add(grid, h('div', {}, h('button', { class: 'btn small', type: 'button', text: '+ strictness', onclick: () => {
      item.items.push(adopt({ kind: 'field', key: 'STRICTNESS', value: { text: 'ALL', kind: 'keyword' } }, item)); listChanged(item); drawScripts();
    } })));
  }
  if (top && !fields.some(f => f.key === 'THEMES')) {
    add(grid, h('div', {}, h('button', { class: 'btn small', type: 'button', title: 'Compile this script with only some themes. Without THEMES it compiles with every enabled theme.', text: '+ themes', onclick: () => {
      item.items.push(adopt({ kind: 'field', key: 'THEMES', value: { text: '', kind: 'string' } }, item)); listChanged(item); drawScripts();
    } })));
  }
  const comments = item.items.filter(i => i.kind === 'comment');
  if (!top) return h('div', { class: 'stack', style: { gap: '6px' } }, grid, comments.map(c => h('p', { class: 'hint mono', text: c.text })));
  return h('div', { class: 'edt-card', id: 'sc-item-' + index }, h('div', { class: 'edt-row' }, h('h3', { text: 'Script settings' }), h('span', { class: 'hint', text: 'NAME names the compiled filter; BUILD picks the class or equipment preset the progression rules follow; THEMES (optional) lists the theme names it compiles with, such as aryzel original. A name is a theme file name or its first words.' }), toolbar && toolbar()), grid, comments.map(c => h('p', { class: 'hint mono', text: c.text })));
}

function valuePicker(value, onChange, { kind = 'style', label = 'Style' } = {}) {
  if (kind === 'category') {
    const select = h('select', { class: 'edt', 'aria-label': label, onchange: e => { value.text = e.target.value; value.kind = 'string'; onChange(); } },
      [...new Set([value.text, ...sc.catalog.categories])].filter(v => v !== undefined).map(c => h('option', { value: c, text: c || '(choose)', selected: c === value.text })));
    return select;
  }
  const input = h('input', { class: 'edt mono fill', value: value.text, 'aria-label': label, placeholder: 'Final/… or $variable', spellcheck: false });
  input.setAttribute('list', 'dl-script-styles');
  const check = () => {
    const v = input.value.trim();
    const known = v.startsWith('$') ? scriptVars().includes(v) : sc.catalog.styles.includes(v);
    const partial = !v.startsWith('$') && sc.catalog.partialStyles.includes(v);
    input.classList.toggle('invalid', !known && !partial);
    input.title = known ? '' : partial ? 'Only some enabled themes\' stylesheets have this style' : v.startsWith('$') ? 'No variable has this name' : 'No enabled theme\'s stylesheet has this style';
  };
  input.addEventListener('input', () => { value.text = input.value.trim(); value.kind = value.text.startsWith('$') ? 'var' : 'string'; check(); onChange(); });
  check();
  return input;
}

function varView(item, index, toolbar, trailing) {
  const rows = item.assignments.map((a, ai) => {
    const name = h('input', { class: 'edt mono', value: a.name, style: { width: '190px' }, 'aria-label': 'Variable name' });
    name.addEventListener('input', () => {
      a.name = name.value.trim(); name.classList.toggle('invalid', !/^[A-Za-z_][A-Za-z0-9_.-]*$/.test(a.name)); touch(item); refreshStyleList();
    });
    const values = h('span', { class: 'edt-row', style: { flex: '1 1 420px' } });
    const drawValues = () => {
      fill(values, );
      a.values.forEach((v, vi) => {
        if (vi) add(values, h('span', { class: 'arrow', text: '+' }));
        add(values, valuePicker(v, () => touch(item)), a.values.length > 1 ? iconButton('×', 'Remove this style', () => { a.values.splice(vi, 1); touch(item); drawValues(); }) : null);
      });
      add(values, iconButton('+', 'Combine another style (later ones win)', () => { a.values.push({ text: '', kind: 'string' }); touch(item); drawValues(); }));
    };
    drawValues();
    const overrides = h('div', { class: 'edt-list' });
    const drawOverrides = () => {
      fill(overrides, );
      (a.overrides || []).forEach((block, bi) => block.forEach((o, oi) => {
        const source = h('select', { class: 'edt mono', onchange: e => { o.source = JSON.parse(e.target.value); touch(item); } },
          a.values.map(v => h('option', { value: JSON.stringify(v), text: v.text, selected: v.text === o.source.text })));
        const property = h('select', { class: 'edt mono', onchange: e => { o.property = e.target.value; touch(item); } },
          ['TextColor', 'BorderColor', 'BackgroundColor', 'FontSize', 'DropSound', 'DropVolume', 'Minimap.Shape', 'Minimap.Color', 'Minimap.Size', 'Beam.Color', 'Beam.Temp']
            .map(p => h('option', { value: p, text: p, selected: p === o.property })));
        add(overrides, h('div', { class: 'edt-row' }, h('span', { class: 'hint', text: 'takes' }), property, h('span', { class: 'hint', text: 'from' }), source,
          iconButton('×', 'Remove', () => { block.splice(oi, 1); if (!block.length) a.overrides.splice(bi, 1); if (!a.overrides.length) delete a.overrides; touch(item); drawOverrides(); })));
      }));
    };
    drawOverrides();
    return h('div', { class: 'stack', style: { gap: '4px' } },
      h('div', { class: 'edt-row' }, h('span', { class: 'kw', text: ai ? '->' : 'var' }), name, h('span', { class: 'arrow', text: '=>' }), values,
        a.values.length > 1 ? h('button', { class: 'btn small', type: 'button', text: '+ override', title: 'Pick which combined style wins a property', onclick: () => {
          a.overrides = a.overrides || [];
          if (!a.overrides.length) a.overrides.push([]);
          a.overrides[a.overrides.length - 1].push({ source: clone(a.values[a.values.length - 1]), property: 'BorderColor' });
          touch(item); drawOverrides();
        } }) : null),
      overrides);
  });
  return h('div', { class: 'edt-item', id: 'sc-item-' + index }, h('div', { class: 'stack', style: { gap: '4px', flex: '1 1 600px' } }, rows), trailing, toolbar());
}

function sectionView(item, index, toolbar) {
  const name = sectionName(item);
  const meta = item.items.find(i => i.kind === 'metadata');
  const desc = meta && meta.items.find(f => f.kind === 'field' && f.key === 'DESCRIPTION');
  const key = 'sec:' + name;
  const details = h('details', { class: 'edt-section', id: 'sc-item-' + index, open: sc.open.has(key) });
  const summary = h('summary', {}, h('span', { class: 'nm', text: name }), h('span', { class: 'ds', text: desc && desc.value ? desc.value.text : '' }),
    h('span', { class: 'pv-sum', text: `${countRules(item)} rule${countRules(item) === 1 ? '' : 's'}` }), toolbar());
  const body = h('div', { class: 'body' });
  const build = () => {
    if (body.childElementCount) return;
    drawItems(body, item.items, item, 'section');
    const has = kind => item.items.some(i => i.kind === kind);
    const extras = [];
    if (!has('conditions')) extras.push(h('button', { class: 'btn small', type: 'button', text: '+ section conditions', onclick: () => { const at = item.items.findIndex(i => i.kind === 'metadata') + 1; item.items.splice(at, 0, adopt({ kind: 'conditions', items: [] }, item)); listChanged(item); fill(body, ); build(); } }));
    if (!has('rules')) extras.push(h('button', { class: 'btn small', type: 'button', text: '+ rules block', onclick: () => { item.items.push(adopt({ kind: 'rules', items: [] }, item)); listChanged(item); fill(body, ); build(); } }));
    if (extras.length) add(body, h('div', { class: 'edt-row' }, extras));
  };
  details.addEventListener('toggle', () => {
    if (details.open) { sc.open.add(key); build(); } else sc.open.delete(key);
    store.set('sc-open', [...sc.open]);
  });
  // The summary's controls act on the section without opening or closing it.
  summary.addEventListener('click', e => { if (e.target.closest('.edt-tools') && !e.target.closest('select')) e.preventDefault(); });
  add(details, summary, body);
  if (details.open) build();
  return details;
}

function blockView(item, title, context, help, trailing) {
  const host = h('div', { class: 'stack', style: { gap: '0' } });
  drawItems(host, item.items, item, context);
  return h('div', { class: 'edt-block' }, h('div', { class: 'edt-row' }, h('span', { class: 'lbl-sm', text: title }), h('span', { class: 'hint', text: help }), trailing), host);
}

function conditionsEditor(owner) {
  const chain = h('div', { class: 'edt-chain' });
  const draw = () => {
    fill(chain, );
    owner.conditions.forEach((c, i) => {
      const spec = sc.conditions.get(c.id);
      const idSelect = h('select', { class: 'edt', 'aria-label': 'Condition', onchange: e => {
        const fresh = newCondition(e.target.value);
        c.id = fresh.id; c.value = fresh.value;
        if (!sc.catalog.operators.includes(c.op)) c.op = '==';
        touch(owner); draw();
      } }, [...new Set([c.id, ...sc.catalog.conditions.map(x => x.id)])].map(id => h('option', { value: id, text: (sc.conditions.get(id) || { label: id }).label, title: (sc.conditions.get(id) || {}).help || '', selected: id === c.id })));
      const op = h('select', { class: 'edt mono', 'aria-label': 'Comparison', onchange: e => { c.op = e.target.value; touch(owner); } },
        sc.catalog.operators.map(o => h('option', { value: o, text: o, selected: o === c.op })));
      add(chain, h('span', { class: 'edt-cond', title: spec ? spec.help : '' }, i ? null : h('span', { class: 'kw', text: 'WHERE' }), idSelect, op, conditionValue(c, spec, () => touch(owner)),
        owner.conditions.length > 1 ? iconButton('×', 'Remove this condition', () => { owner.conditions.splice(i, 1); touch(owner); draw(); }) : null));
      if (i < owner.conditions.length - 1) add(chain, h('span', { class: 'arrow', text: '->' }));
    });
    add(chain, iconButton('+', 'Add a condition (all must hold)', () => { owner.conditions.push(newCondition()); touch(owner); draw(); }));
  };
  draw();
  return chain;
}
function conditionValue(c, spec, onChange) {
  const setText = text => {
    const previous = c.value.kind;
    let kind;
    if (text.startsWith('$')) kind = 'var';
    else if (previous === 'number' && /^-?\d+(\.\d+)?$/.test(text)) kind = 'number';
    else if (previous === 'number' || previous === 'var') kind = spec && !spec.quoted && /^-?\d+(\.\d+)?$/.test(text) ? 'number' : 'string';
    else kind = previous === 'keyword' && /^[\w.-]+$/.test(text) ? 'keyword' : 'string';
    c.value = { text, kind };
    onChange();
  };
  if (spec && spec.kind === 'choice') {
    return h('select', { class: 'edt', 'aria-label': 'Value', onchange: e => setText(e.target.value) },
      [...new Set([c.value.text, ...spec.choices])].map(v => h('option', { value: v, text: v, selected: v === c.value.text })));
  }
  const width = { number: '70px', sockets: '80px', baseType: '240px', itemClass: '200px' }[spec ? spec.kind : ''] || '160px';
  const input = h('input', { class: 'edt mono', value: c.value.text, style: { width }, 'aria-label': 'Value', spellcheck: false,
    placeholder: spec && spec.kind === 'sockets' ? '5, RGB, 4RGB' : '' });
  if (spec && spec.kind === 'baseType') input.setAttribute('list', 'dl-basetypes');
  if (spec && spec.kind === 'itemClass') input.setAttribute('list', 'dl-classes');
  const check = () => {
    const v = input.value.trim();
    let bad = /[^\p{L}\p{N} \t._[\]\-/'$]/u.test(v);
    if (spec && spec.kind === 'number' && !v.startsWith('$')) bad = bad || !/^-?\d+(\.\d+)?$/.test(v);
    if (spec && spec.kind === 'baseType' && c.op === '==' && !v.startsWith('$')) input.title = sc.catalog.baseTypes.includes(v) ? '' : 'Not a known base type (it may still be valid)';
    input.classList.toggle('invalid', bad);
  };
  input.addEventListener('input', () => { setText(input.value.trim()); check(); });
  check();
  return input;
}

function ruleView(item, toolbar, trailing) {
  const action = h('select', { class: 'edt', 'aria-label': 'Action', onchange: e => { item.action = { text: e.target.value, kind: 'var' }; touch(item); } },
    ['$Show', '$Hide'].map(a => h('option', { value: a, text: a.slice(1), selected: a === item.action.text })));
  const strictness = h('select', { class: 'edt mono', 'aria-label': 'Strictness', title: 'Optional # STRICTNESS suffix', onchange: e => { item.strictness = e.target.value || undefined; touch(item); } },
    h('option', { value: '', text: '#', selected: !item.strictness }), sc.catalog.strictnesses.map(s => h('option', { value: s, text: '# ' + s, selected: s === item.strictness })));
  return h('div', { class: 'edt-item' }, conditionsEditor(item), h('span', { class: 'arrow', text: '=>' }),
    valuePicker(item.style, () => touch(item)), h('span', { class: 'arrow', text: '=>' }), action, strictness, trailing, toolbar());
}

function paramRole(spec, key) {
  if (!spec) return null;
  for (const p of spec.params) {
    if (p.key === key) return p;
    if (p.key.endsWith('_N') && key.startsWith(p.key.slice(0, -1)) && /^\d+$/.test(key.slice(p.key.length - 1))) return p;
  }
  return null;
}
// A one-line macro that grows a second parameter moves to one parameter per line.
function settleInline(item) {
  if (item.items.filter(p => p.kind === 'param').length > 1) item.inline = false;
}
function macroView(item, toolbar, trailing) {
  const spec = sc.macros.get(item.name);
  const nameSelect = h('select', { class: 'edt mono', 'aria-label': 'Macro', onchange: e => {
    const next = sc.macros.get(e.target.value);
    const bothTiered = next && next.tiers && spec && spec.tiers;
    // Keep the parameters the new macro uses, and the tiers when both tier.
    item.items = item.items.filter(p => p.kind !== 'param' || paramRole(next, p.key) || (bothTiered && !paramRole(spec, p.key)));
    item.name = e.target.value;
    macroWithDefaults(item.name).items.forEach(p => {
      const isTier = next && next.tiers && !paramRole(next, p.key);
      const hasTier = item.items.some(x => x.kind === 'param' && !paramRole(next, x.key));
      if ((isTier && !hasTier) || (!isTier && !item.items.some(x => x.key === p.key))) item.items.push(p);
    });
    settleInline(item);
    indexParents(item.items, item); touch(item); redrawMacro();
  } }, [...new Set([item.name, ...sc.catalog.macros.map(m => m.name)])].map(n => h('option', { value: n, text: n, selected: n === item.name })));
  const params = h('div', { class: 'edt-params' });
  const box = h('div', { class: 'edt-item macro' },
    h('div', { class: 'edt-row' }, h('span', { class: 'kw', text: 'MACRO' }), nameSelect, spec ? null : h('span', { class: 'edt-status bad', text: 'Unknown macro' }), trailing, toolbar()),
    spec ? h('div', { class: 'edt-macro-sum', text: spec.summary }) : null, params);
  const redrawMacro = () => { const fresh = macroView(item, toolbar, trailing); box.replaceWith(fresh); };
  const tierParams = () => item.items.filter(p => p.kind === 'param' && spec && spec.tiers && !paramRole(spec, p.key));
  item.items.forEach((p, i) => {
    if (p.kind === 'comment') { add(params, h('p', { class: 'hint mono', text: p.text })); return; }
    if (p.kind !== 'param') return;
    const role = paramRole(spec, p.key);
    const tiers = tierParams();
    const tierIndex = tiers.indexOf(p);
    const label = tierIndex >= 0 ? `Tier ${tierIndex + 1} (${p.key})` : p.key;
    const move = d => {
      const j = tiers[tierIndex + d];
      if (!j) return;
      const a = item.items.indexOf(p), b = item.items.indexOf(j);
      [item.items[a], item.items[b]] = [item.items[b], item.items[a]];
      touch(item); redrawMacro();
    };
    const removable = !(role && role.required);
    add(params, h('div', { class: 'edt-param', title: role ? role.summary : tierIndex >= 0 ? 'Economy tiers, best first' : 'This macro does not use this parameter' },
      h('span', { class: 'key', text: label }),
      valuePicker(p.value, () => touch(item), { kind: role ? role.value : 'style', label }),
      tools(tierIndex >= 0 ? [iconButton('↑', 'Better tier', () => move(-1), tierIndex === 0), iconButton('↓', 'Worse tier', () => move(1), tierIndex === tiers.length - 1)] : null,
        removable ? iconButton('×', 'Remove', () => { item.items.splice(i, 1); touch(item); redrawMacro(); }) : null)));
  });
  const adders = [];
  if (spec && spec.tiers) {
    adders.push(h('button', { class: 'btn small', type: 'button', text: '+ tier', onclick: () => {
      const tiers = tierParams();
      let n = tiers.length; while (item.items.some(p => p.key === `$tier_${n}`)) n++;
      const at = tiers.length ? item.items.indexOf(tiers[tiers.length - 1]) + 1 : 0;
      item.items.splice(at, 0, adopt({ kind: 'param', key: `$tier_${n}`, value: { text: '', kind: 'string' } }, item));
      settleInline(item); touch(item); redrawMacro();
    } }));
  }
  (spec ? spec.params : []).forEach(p => {
    const present = item.items.some(x => x.kind === 'param' && paramRole(spec, x.key) === p);
    if (p.key.endsWith('_N')) {
      adders.push(h('button', { class: 'btn small', type: 'button', text: `+ ${p.key}`, title: p.summary, onclick: () => {
        const n = prompt(`${p.summary}\nThreshold N:`, '20');
        if (!n || !/^\d+$/.test(n)) return;
        const key = p.key.slice(0, -1) + n;
        if (item.items.some(x => x.key === key)) return;
        item.items.push(adopt({ kind: 'param', key, value: defaultParamValue(p) }, item)); settleInline(item); touch(item); redrawMacro();
      } }));
    } else if (!present) {
      adders.push(h('button', { class: 'btn small', type: 'button', text: `+ ${p.key}`, title: p.summary, onclick: () => {
        item.items.push(adopt({ kind: 'param', key: p.key, value: defaultParamValue(p) }, item)); settleInline(item); touch(item); redrawMacro();
      } }));
    }
  });
  if (adders.length) add(params, h('div', { class: 'edt-row' }, adders));
  return box;
}

function scriptRequest(dryRun) {
  return { document: sc.doc, hash: sc.hash, create: sc.create, dryRun };
}
async function saveScript(dryRun, status) {
  if (!dryRun && !mayReload('scripts')) return;
  setStatus(status, dryRun ? 'Checking: compiling the script with every enabled theme…' : 'Saving: checking, writing and recompiling every filter…');
  try {
    const result = await api('PUT', `/api/scripts/${encodeURIComponent(sc.file)}`, scriptRequest(dryRun));
    if (dryRun) { setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok'); return; }
    store.set('editor:script', sc.file);
    reloadInto('scripts');
  } catch (e) { setStatus(status, e.message, 'bad'); }
}
async function scriptChanges(status) {
  setStatus(status, 'Preparing the script…');
  try {
    const result = await api('PUT', `/api/scripts/${encodeURIComponent(sc.file)}`, scriptRequest(true));
    setStatus(status, 'Checks passed.\n' + describeResult(result), 'ok');
    showDialog(`Changes to ${sc.file}`, diffView(result.original, result.contents));
  } catch (e) { setStatus(status, e.message, 'bad'); }
}

function newScriptDialog() {
  if (!confirmDiscard('scripts')) return;
  const name = h('input', { class: 'edt', placeholder: 'MappingFilter' });
  const file = h('input', { class: 'edt mono', placeholder: 'mapping_filter.rf' });
  let fileEdited = false;
  file.addEventListener('input', () => { fileEdited = true; });
  name.addEventListener('input', () => { if (!fileEdited) file.value = name.value.replace(/([a-z])([A-Z])/g, '$1_$2').toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') + '.rf'; });
  const build = h('select', { class: 'edt' }, [...sc.catalog.presets.map(p => ['string', p]), ...sc.catalog.builds.map(b => ['keyword', b])].map(([kind, text]) => h('option', { value: JSON.stringify([kind, text]), text })));
  const problem = h('p', { class: 'edt-status bad' });
  const dialog = showDialog('New script', h('div', { class: 'edt-grid' }, field('Filter name', name, 'NAME: the compiled filter is "<name> (<theme suffix>).filter"'), field('File name', file), field('Build', build), problem), [
    h('button', { class: 'btn primary', type: 'button', text: 'Create', onclick: async () => {
      let f = file.value.trim();
      if (f && !f.endsWith('.rf')) f += '.rf';
      if (!/^[A-Za-z0-9][A-Za-z0-9._ -]*\.rf$/.test(f) || !/^[\p{L}\p{N}._ -]+$/u.test(name.value.trim())) { problem.textContent = 'Give the script a filter name and a file name (letters, digits, spaces, . _ -).'; return; }
      if (sc.files.includes(f)) { problem.textContent = `${f} already exists.`; return; }
      const [kind, text] = JSON.parse(build.value);
      const doc = { finalNewline: true, items: [
        { kind: 'metadata', items: [
          { kind: 'field', key: 'NAME', value: { text: name.value.trim(), kind: 'string' } },
          { kind: 'field', key: 'VERSION', value: { text: '0.1.0', kind: 'string' } },
          { kind: 'field', key: 'STRICTNESS', value: { text: 'ALL', kind: 'keyword' } },
          { kind: 'field', key: 'BUILD', value: { text, kind } }] },
        { kind: 'blank', count: 1 },
        newItem('section')] };
      await loadScript(f, doc);
      dialog.close(); drawScripts();
      setStatus(sc.statusEl, 'Add rules, then Save to create the file.', 'warn');
    } }),
  ]);
  name.focus();
}

})();
