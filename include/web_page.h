#pragma once

// Single-page web UI served at "/". Plain HTML/CSS/JS, no external files (works offline in setup mode).
static const char WebPage[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PowderReload</title>
<style>
:root{--bg:#07090b;--panel:#11161b;--line:#1f2830;--text:#e8edf1;--dim:#7d8b96;--cyan:#00cde4;--blue:#0a8cf8;--green:#23ff00;--amber:#ff9600;--red:#ff3c3c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:14px 18px;border-bottom:1px solid var(--line)}
.logo{color:var(--cyan);font-weight:800;font-style:italic;font-size:20px}
.chip{margin-left:auto;display:flex;align-items:center;gap:8px;font-weight:700;font-size:14px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--green)}
nav{display:flex;gap:4px;padding:8px 12px;border-bottom:1px solid var(--line);overflow-x:auto}
nav button{background:none;border:0;color:var(--dim);font:inherit;font-weight:600;padding:8px 14px;border-radius:8px;cursor:pointer}
nav button.on{color:var(--text);background:var(--panel)}
main{max-width:900px;margin:0 auto;padding:18px}
section{display:none}section.on{display:block}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
.big{text-align:center;font-weight:800;font-size:clamp(64px,18vw,120px);line-height:1;margin:10px 0 4px}
.big small{font-size:.45em;margin-left:6px}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden;margin:16px 0}
.bar div{height:100%;width:0;background:var(--cyan);transition:width .2s}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.grow{flex:1}
.label{color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:.06em;text-transform:uppercase}
.muted{color:var(--dim)}
button.btn{font:inherit;font-weight:700;border-radius:10px;padding:12px 18px;border:1px solid var(--line);background:#18212a;color:var(--text);cursor:pointer}
button.primary{background:var(--blue);border-color:var(--blue)}
button.danger{color:var(--red)}
button.small{padding:6px 12px;font-size:13px}
button:disabled{opacity:.4;cursor:default}
select,input,textarea{width:100%;font:inherit;color:var(--text);background:#0b0f13;border:1px solid var(--line);border-radius:8px;padding:10px}
textarea{min-height:70px;resize:vertical}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:12px}
.grid label{display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--dim)}
.grid .wide{grid-column:1/-1}
.recipe{display:flex;gap:12px;align-items:center;padding:12px 0;border-top:1px solid var(--line)}
.recipe:first-of-type{border-top:0}
.recipe b{font-size:16px}
.tag{font-size:11px;font-weight:700;color:var(--green);border:1px solid var(--green);border-radius:6px;padding:1px 6px;margin-left:6px}
.stats{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:10px}
.stats div{background:#0b0f13;border-radius:8px;padding:10px}
.stats b{display:block;font-size:20px}
table{width:100%;border-collapse:collapse;font-size:14px}
th,td{text-align:left;padding:8px 6px;border-bottom:1px solid var(--line)}
th{color:var(--dim);font-weight:600}
.ok{color:var(--green)}.over{color:var(--red)}.stop{color:var(--amber)}
.net{display:flex;justify-content:space-between;padding:10px;border-radius:8px;cursor:pointer}
.net:hover,.net.on{background:#18212a}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#18212a;border:1px solid var(--line);padding:10px 16px;border-radius:10px;display:none}
</style>
</head>
<body>
<header><span class="logo">PowderReload</span><span class="chip"><span class="dot" id="dot"></span><span id="status">…</span></span></header>
<nav>
<button data-tab="live" class="on">Live</button><button data-tab="recipes">Recipes</button><button data-tab="log">Log</button><button data-tab="wifi">Wi-Fi</button>
</nav>
<main>
<section id="live" class="on">
  <div class="card">
    <div class="big"><span id="weight">0.0</span><small>gr</small></div>
    <div class="bar"><div id="progress"></div></div>
    <div class="row">
      <div class="grow"><div class="label">Recipe</div><select id="liveRecipe"></select></div>
      <div><div class="label">Target</div><div style="font-size:28px;font-weight:800"><span id="target">0.0</span> gr</div></div>
    </div>
  </div>
  <div class="row">
    <button class="btn primary grow" id="fill">Fill</button>
    <button class="btn grow" id="stop">Stop</button>
    <button class="btn grow" id="tare">Tare</button>
  </div>
</section>

<section id="recipes">
  <div class="card" id="recipeList"></div>
  <button class="btn primary" id="newRecipe">+ New recipe</button>
  <div class="card" id="editor" style="display:none;margin-top:14px">
    <div class="row" style="margin-bottom:12px"><b class="grow" id="editorTitle">Recipe</b><span class="muted" id="editorDate"></span></div>
    <div class="grid">
      <label class="wide">Name<input id="f_name" maxlength="40" placeholder="6.5 CM match"></label>
      <label>Caliber<input id="f_caliber" maxlength="30" placeholder="6.5 Creedmoor"></label>
      <label>Bullet<input id="f_bullet" maxlength="40" placeholder="Hornady ELD-M"></label>
      <label>Bullet weight (gr)<input id="f_bulletWeight" type="number" step="0.1" min="0"></label>
      <label>Powder<input id="f_powder" maxlength="30" placeholder="H4350"></label>
      <label>Charge (gr)<input id="f_charge" type="number" step="0.1" min="0" max="99.9"></label>
      <label>Primer<input id="f_primer" maxlength="30"></label>
      <label>Brass<input id="f_brass" maxlength="30"></label>
      <label>OAL<input id="f_oal" maxlength="20" placeholder="2.800 in"></label>
      <label>CBTO<input id="f_cbto" maxlength="20" placeholder="2.230 in"></label>
      <label>Fast speed (RPM)<input id="f_fastRpm" type="number" min="1" max="225"></label>
      <label>Slow speed (RPM)<input id="f_slowRpm" type="number" min="1" max="225"></label>
      <label>Slow at (gr before target)<input id="f_slowAt" type="number" step="0.1" min="0.1" max="20"></label>
      <label class="wide">Notes<textarea id="f_notes" maxlength="500"></textarea></label>
    </div>
    <div class="row" style="margin-top:14px">
      <button class="btn primary" id="save">Save</button>
      <button class="btn" id="duplicate">Duplicate</button>
      <button class="btn" id="cancel">Cancel</button>
      <span class="grow"></span>
      <button class="btn danger" id="delete">Delete</button>
    </div>
  </div>
</section>

<section id="log">
  <div class="card">
    <div class="row" style="margin-bottom:12px">
      <div class="grow"><select id="logFilter"></select></div>
      <button class="btn small" id="download">Download CSV</button>
      <button class="btn small danger" id="clearLog">Clear</button>
    </div>
    <div class="stats" id="stats"></div>
  </div>
  <div class="card"><table><thead><tr><th>Date</th><th>Recipe</th><th>Target</th><th>Actual</th><th>Diff</th><th></th></tr></thead><tbody id="logRows"></tbody></table></div>
</section>

<section id="wifi">
  <div class="card"><div class="label">Status</div><div id="wifiStatus" style="margin-top:6px"></div></div>
  <div class="card">
    <div class="row" style="margin-bottom:8px"><b class="grow">Join a network</b><button class="btn small" id="scan">Scan</button></div>
    <div id="networks" class="muted">Press Scan to list networks.</div>
    <div class="grid" style="margin-top:12px">
      <label>Network<input id="ssid"></label>
      <label>Password<input id="password" type="password"></label>
    </div>
    <button class="btn primary" id="join" style="margin-top:12px">Save and restart</button>
    <p class="muted">The board restarts and joins this network. Then open <b>http://powderreload.local</b> from a phone or PC on that network. If it cannot connect, it opens its own network <b>PowderReload</b> (password <b>powderreload</b>) again.</p>
  </div>
</section>
</main>
<div id="toast"></div>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const g1 = v => Number(v || 0).toFixed(1);
let recipes = [], active = 0, revision = -1, editing = null, logRows = [];

function toast(text) { const t = $('toast'); t.textContent = text; t.style.display = 'block'; clearTimeout(t.timer); t.timer = setTimeout(() => t.style.display = 'none', 2500); }
async function api(path, body) {
  const r = await fetch(path, body === undefined ? {} : {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
  const type = r.headers.get('content-type') || '';
  const data = type.includes('json') ? await r.json() : await r.text();
  if (!r.ok) throw new Error(data.error || r.statusText);
  return data;
}
const run = (fn) => async () => { try { await fn(); } catch (e) { toast(e.message); } };

// ---------------------------------------------------------------- tabs
document.querySelectorAll('nav button').forEach(b => b.onclick = () => {
  document.querySelectorAll('nav button, section').forEach(e => e.classList.remove('on'));
  b.classList.add('on'); $(b.dataset.tab).classList.add('on');
  if (b.dataset.tab === 'log') loadLog();
});

// ---------------------------------------------------------------- live
const colors = {'FILLING':'--blue','STOPPING':'--amber','STOPPED':'--amber','SET TARGET':'--amber','MOTOR FAULT':'--red','OVER TARGET':'--red'};
async function poll() {
  try {
    const s = await api('/api/status');
    $('status').textContent = s.status;
    $('dot').style.background = `var(${colors[s.status] || '--green'})`;
    $('weight').textContent = g1(s.weight);
    $('target').textContent = g1(s.target);
    const pct = s.target > 0 ? Math.min(100, s.weight / s.target * 100) : 0;
    $('progress').style.width = pct + '%';
    $('progress').style.background = s.status === 'ON TARGET' ? 'var(--green)' : s.status === 'OVER TARGET' ? 'var(--red)' : 'var(--cyan)';
    $('weight').style.color = s.status === 'ON TARGET' ? 'var(--green)' : s.status === 'OVER TARGET' ? 'var(--red)' : '';
    $('fill').disabled = s.filling; $('stop').disabled = !s.filling; $('liveRecipe').disabled = s.filling;
    const w = s.wifi;
    $('wifiStatus').innerHTML = w.mode === 'connected'
      ? `Connected to <b>${esc(w.network)}</b> · ${esc(w.address)} · signal ${w.rssi} dBm`
      : w.mode === 'setup' ? `Setup network <b>${esc(w.network)}</b> · ${esc(w.address)}` : 'Connecting…';
    if (s.revision !== revision) { revision = s.revision; await loadRecipes(); }
    if (!s.clockSet) api('/api/time', {time: Math.floor(Date.now() / 1000)}).catch(() => {});
  } catch (e) { $('status').textContent = 'OFFLINE'; $('dot').style.background = 'var(--red)'; }
  setTimeout(poll, document.querySelector('#live.on') ? 300 : 1500);
}
$('fill').onclick = run(() => api('/api/fill', {}));
$('stop').onclick = run(() => api('/api/stop', {}));
$('tare').onclick = run(() => api('/api/tare', {}));
$('liveRecipe').onchange = run(() => api('/api/recipes/select?id=' + $('liveRecipe').value, {}));

// ---------------------------------------------------------------- recipes
const fields = ['name','caliber','bullet','bulletWeight','powder','charge','primer','brass','oal','cbto','fastRpm','slowRpm','slowAt','notes'];
async function loadRecipes() {
  const d = await api('/api/recipes');
  recipes = d.recipes; active = d.active;
  $('liveRecipe').innerHTML = recipes.map(r => `<option value="${r.id}" ${r.id === active ? 'selected' : ''}>${esc(r.name)} · ${g1(r.charge)} gr</option>`).join('');
  $('recipeList').innerHTML = recipes.map(r => `
    <div class="recipe">
      <div class="grow"><b>${esc(r.name)}</b>${r.id === active ? '<span class="tag">IN USE</span>' : ''}
        <div class="muted">${[r.caliber, r.bulletWeight ? g1(r.bulletWeight) + ' gr ' + r.bullet : r.bullet, r.powder].filter(Boolean).map(esc).join(' · ')}</div></div>
      <div style="text-align:right"><b>${g1(r.charge)} gr</b><div class="muted">${r.oal ? 'OAL ' + esc(r.oal) : ''}</div></div>
      <button class="btn small" onclick="useRecipe(${r.id})" ${r.id === active ? 'disabled' : ''}>Use</button>
      <button class="btn small" onclick="editRecipe(${r.id})">Edit</button>
    </div>`).join('');
  const keep = $('logFilter').value;
  $('logFilter').innerHTML = '<option value="">All recipes</option>' + recipes.map(r => `<option>${esc(r.name)}</option>`).join('');
  $('logFilter').value = keep;
}
window.useRecipe = id => run(() => api('/api/recipes/select?id=' + id, {}))();
window.editRecipe = id => openEditor(recipes.find(r => r.id === id));
function openEditor(r) {
  editing = r || {id: 0, name: '', charge: 0, fastRpm: 120, slowRpm: 15, slowAt: 2};
  fields.forEach(f => $('f_' + f).value = editing[f] ?? '');
  $('editorTitle').textContent = editing.id ? 'Edit recipe' : 'New recipe';
  $('editorDate').textContent = editing.updated ? 'Updated ' + new Date(editing.updated * 1000).toLocaleDateString() : '';
  $('delete').style.display = $('duplicate').style.display = editing.id ? '' : 'none';
  $('editor').style.display = 'block';
  $('editor').scrollIntoView({behavior: 'smooth'});
}
function formRecipe() {
  const r = {id: editing.id};
  fields.forEach(f => { const el = $('f_' + f); r[f] = el.type === 'number' ? Number(el.value || 0) : el.value.trim(); });
  return r;
}
$('newRecipe').onclick = () => openEditor();
$('cancel').onclick = () => $('editor').style.display = 'none';
$('save').onclick = run(async () => { await api('/api/recipes', formRecipe()); $('editor').style.display = 'none'; toast('Saved'); });
$('duplicate').onclick = run(async () => { const r = formRecipe(); r.id = 0; r.name += ' copy'; const d = await api('/api/recipes', r); revision = -1; toast('Duplicated'); });
$('delete').onclick = run(async () => {
  if (!confirm('Delete "' + editing.name + '"?')) return;
  await api('/api/recipes/delete?id=' + editing.id, {}); $('editor').style.display = 'none'; toast('Deleted');
});

// ---------------------------------------------------------------- log
function parseLog(text) {
  return text.split('\n').filter(Boolean).map(line => {
    const p = line.split(',');
    const name = p.slice(4).join(',').replace(/^"|"$/g, '').replace(/""/g, '"');
    return {t: Number(p[0]), target: Number(p[1]), actual: Number(p[2]), result: p[3], name};
  }).reverse();
}
async function loadLog() { logRows = parseLog(await api('/api/log')); showLog(); }
function showLog() {
  const filter = $('logFilter').value;
  const rows = logRows.filter(r => !filter || r.name === filter);
  const done = rows.filter(r => r.result !== 'STOP').map(r => r.actual);
  const mean = done.reduce((a, b) => a + b, 0) / (done.length || 1);
  const sd = Math.sqrt(done.reduce((a, b) => a + (b - mean) ** 2, 0) / Math.max(1, done.length - 1));
  const es = done.length ? Math.max(...done) - Math.min(...done) : 0;
  $('stats').innerHTML = [['Fills', rows.length], ['Average', done.length ? g1(mean) : '–'], ['SD', done.length > 1 ? sd.toFixed(2) : '–'],
    ['Spread', done.length ? g1(es) : '–'], ['Over', rows.filter(r => r.result === 'OVER').length]]
    .map(([k, v]) => `<div><span class="muted">${k}</span><b>${v}</b></div>`).join('');
  $('logRows').innerHTML = rows.slice(0, 300).map(r => `<tr>
    <td>${r.t ? new Date(r.t * 1000).toLocaleString([], {dateStyle: 'short', timeStyle: 'short'}) : '–'}</td>
    <td>${esc(r.name)}</td><td>${g1(r.target)}</td><td>${g1(r.actual)}</td>
    <td>${(r.actual - r.target >= 0 ? '+' : '') + (r.actual - r.target).toFixed(1)}</td>
    <td class="${r.result.toLowerCase()}">${esc(r.result)}</td></tr>`).join('') || '<tr><td colspan="6" class="muted">No fills yet.</td></tr>';
}
$('logFilter').onchange = showLog;
$('download').onclick = () => location.href = '/api/log?download=1';
$('clearLog').onclick = run(async () => { if (!confirm('Clear the whole log?')) return; await api('/api/log/clear', {}); loadLog(); });

// ---------------------------------------------------------------- wifi
$('scan').onclick = run(async () => {
  $('networks').textContent = 'Scanning…';
  const list = await api('/api/scan');
  list.sort((a, b) => b.rssi - a.rssi);
  $('networks').innerHTML = list.map(n => `<div class="net" data-ssid="${esc(n.ssid)}"><span>${esc(n.ssid)}</span><span class="muted">${n.rssi} dBm${n.secure ? ' 🔒' : ''}</span></div>`).join('') || 'No networks found.';
  document.querySelectorAll('.net').forEach(n => n.onclick = () => { $('ssid').value = n.dataset.ssid; $('password').focus(); });
});
$('join').onclick = run(async () => {
  await api('/api/wifi', {ssid: $('ssid').value.trim(), password: $('password').value});
  toast('Saved. Restarting…');
});

poll();
</script>
</body>
</html>
)HTML";
