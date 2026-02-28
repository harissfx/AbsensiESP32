#pragma once
/*
 * halaman.h — Web Dashboard Absensi ESP32
 * Disimpan di PROGMEM, di-serve oleh WebServer ESP32
 * WebSocket port 81 untuk update realtime
 */

const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Absensi — Panel</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Rajdhani:wght@400;500;600;700&family=Courier+Prime:wght@400;700&display=swap');

/* ── RESET & BASE ───────────────────────────────────── */
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:      #09090e;
  --bg2:     #0e0e16;
  --panel:   #111119;
  --panel2:  #16161f;
  --border:  #1e1e2e;
  --border2: #2a2a3e;
  --amber:   #f0a500;
  --amber2:  #ffcc44;
  --green:   #00e87a;
  --red:     #ff3e5e;
  --blue:    #3eb8ff;
  --muted:   #44445a;
  --text:    #c8c8e0;
  --text2:   #8888a8;
  --mono:    'Courier Prime', monospace;
  --ui:      'Rajdhani', sans-serif;
}
html{ scroll-behavior:smooth }
body{
  font-family: var(--ui);
  background: var(--bg);
  color: var(--text);
  min-height: 100vh;
  /* overflow-x hidden di body bisa menyebabkan sticky header bug di iOS */
}

/* ── SCANLINE OVERLAY ───────────────────────────────── */
body::before{
  content:'';
  position:fixed; inset:0; z-index:999; pointer-events:none;
  background: repeating-linear-gradient(
    0deg,
    transparent,
    transparent 2px,
    rgba(0,0,0,.06) 2px,
    rgba(0,0,0,.06) 4px
  );
}

/* ── HEADER ─────────────────────────────────────────── */
header{
  position: sticky; top:0; z-index:100;
  background: var(--panel);
  border-bottom: 1px solid var(--border);
  min-height: 54px;
  display: flex; align-items: center; justify-content: space-between;
  padding: 0 16px;
  gap: 10px;
  /* overflow hidden cegah child meluber */
  overflow: hidden;
}
.logo-wrap{
  display:flex; align-items:center; gap:10px;
  /* shrink diizinkan tapi tidak sampai hilang */
  flex-shrink: 0; min-width: 0;
}
.logo-icon{
  width:30px; height:30px; border:1px solid var(--amber);
  display:flex; align-items:center; justify-content:center;
  flex-shrink:0;
  clip-path: polygon(8px 0%,100% 0%,100% calc(100% - 8px),calc(100% - 8px) 100%,0% 100%,0% 8px);
  background: #f0a50010;
}
.logo-icon svg{ width:15px; height:15px; fill:var(--amber) }
.logo-text{
  font-family:var(--mono); font-size:12px; font-weight:700;
  color:var(--amber); letter-spacing:2px; text-transform:uppercase;
  white-space:nowrap;
}
.logo-sub{
  font-family:var(--mono); font-size:8px; color:var(--muted);
  letter-spacing:1px; margin-top:1px; white-space:nowrap;
}
.header-right{
  display:flex; align-items:center; gap:8px;
  flex-shrink:0;
}
.ws-pill{
  display:flex; align-items:center; gap:5px;
  font-family:var(--mono); font-size:9px; color:var(--muted);
  padding:4px 8px;
  border:1px solid var(--border2); border-radius:2px;
  letter-spacing:1px; white-space:nowrap;
  transition: border-color .3s, color .3s;
}
.ws-pill.on{ border-color:var(--green); color:var(--green) }
.ws-dot{
  width:6px; height:6px; border-radius:50%; flex-shrink:0;
  background:var(--red); transition:background .4s;
}
.ws-pill.on .ws-dot{
  background:var(--green);
  animation: ping 2s ease-in-out infinite;
}
@keyframes ping{
  0%,100%{ box-shadow:0 0 0 0 #00e87a60 }
  50%{ box-shadow:0 0 0 5px transparent }
}
.uptime-box{
  font-family:var(--mono); font-size:10px; color:var(--amber2);
  padding:4px 8px; border:1px solid #f0a50030; border-radius:2px;
  letter-spacing:1px; white-space:nowrap;
}

/* ── TICKER BAR ─────────────────────────────────────── */
.ticker{
  background:var(--bg2); border-bottom:1px solid var(--border);
  height:26px; overflow:hidden; display:flex; align-items:center;
}
.ticker-label{
  font-family:var(--mono); font-size:9px; font-weight:700;
  color:var(--bg); background:var(--amber); padding:0 10px;
  height:100%; display:flex; align-items:center; letter-spacing:1px;
  flex-shrink:0;
}
.ticker-track{ flex:1; overflow:hidden }
.ticker-inner{
  display:flex; gap:32px; white-space:nowrap;
  font-family:var(--mono); font-size:9px; color:var(--muted);
  letter-spacing:1px;
  animation: ticker-scroll 22s linear infinite;
}
@keyframes ticker-scroll{
  0%{ transform:translateX(0) }
  100%{ transform:translateX(-50%) }
}
.ticker-sep{ color:var(--amber); opacity:.5 }

/* ── MAIN LAYOUT ────────────────────────────────────── */
main{
  max-width:960px; margin:0 auto;
  padding:16px 12px 80px;
}

/* ── STAT GRID ──────────────────────────────────────── */
.stat-grid{
  display:grid;
  grid-template-columns: repeat(3,1fr);
  gap:8px; margin-bottom:20px;
}
.stat-card{
  background:var(--panel); border:1px solid var(--border);
  padding:14px 14px;
  clip-path: polygon(0 0,calc(100% - 10px) 0,100% 10px,100% 100%,0 100%);
  position:relative; min-width:0;
  transition:border-color .2s;
}
.stat-card:hover{ border-color:var(--border2) }
.stat-card::after{
  content:''; position:absolute;
  top:0; right:10px; width:1px; height:10px;
  background: var(--border); transform-origin:bottom;
  transform:rotate(45deg) translate(5px,-5px);
}
.stat-lbl{
  font-size:8px; letter-spacing:1.5px; text-transform:uppercase;
  color:var(--muted); margin-bottom:6px;
}
.stat-val{
  font-family:var(--mono); font-size:24px; line-height:1;
  transition:color .3s;
}
.stat-val.amber{ color:var(--amber) }
.stat-val.green{ color:var(--green) }
.stat-val.blue { color:var(--blue)  }
.stat-bar{
  margin-top:8px; height:2px; background:var(--border);
  overflow:hidden;
}
.stat-bar-fill{
  height:100%; background:var(--amber);
  transition:width .6s ease;
}

/* ── SECTION ────────────────────────────────────────── */
.section{ margin-bottom:24px }
.sec-head{
  display:flex; align-items:center; justify-content:space-between;
  flex-wrap:wrap; gap:8px;
  margin-bottom:10px; padding-bottom:8px;
  border-bottom:1px solid var(--border);
}
.sec-title{
  display:flex; align-items:center; gap:8px;
  font-size:11px; font-weight:600; letter-spacing:2px;
  text-transform:uppercase; color:var(--text2);
  flex-wrap:wrap; gap:6px;
}
.sec-title-bar{
  width:3px; height:12px; background:var(--amber); flex-shrink:0;
}
.sec-actions{ display:flex; gap:6px; flex-wrap:wrap }

/* ── BUTTONS ────────────────────────────────────────── */
.btn{
  font-family:var(--ui); font-size:11px; font-weight:600;
  letter-spacing:1px; text-transform:uppercase;
  padding:5px 12px; cursor:pointer; border:1px solid;
  transition:all .15s; position:relative; overflow:hidden;
  white-space:nowrap;
}
.btn::after{
  content:''; position:absolute; inset:0;
  background:currentColor; opacity:0; transition:opacity .15s;
}
.btn:hover::after{ opacity:.08 }
.btn:active{ transform:scale(.97) }
.btn-amber{
  color:var(--amber); border-color:var(--amber);
  background:transparent; clip-path:polygon(0 0,calc(100% - 6px) 0,100% 6px,100% 100%,0 100%);
}
.btn-green{ color:var(--green); border-color:var(--green); background:transparent }
.btn-red  { color:var(--red);   border-color:var(--red);   background:transparent }
.btn-ghost{ color:var(--text2); border-color:var(--border2); background:transparent }

/* ── TABLE WRAPPER ───────────────────────────────────── */
.tbl-wrap{
  border:1px solid var(--border);
  /* overflow visible — kita kontrol per-tabel via JS class */
  overflow:hidden;
}
table{ width:100%; border-collapse:collapse }
thead tr{
  background:var(--panel2);
  border-bottom:1px solid var(--border2);
}
thead th{
  font-family:var(--mono); font-size:9px; font-weight:700;
  letter-spacing:2px; text-transform:uppercase;
  color:var(--muted); text-align:left;
  padding:9px 12px; white-space:nowrap;
}
tbody tr{
  border-top:1px solid var(--border);
  transition:background .12s;
}
tbody tr:hover{ background:var(--panel2) }
tbody tr.new-row{ animation:rowSlide .5s ease forwards }
@keyframes rowSlide{
  from{ background:#00e87a12; transform:translateX(-4px); opacity:.5 }
  to  { background:transparent; transform:translateX(0);  opacity:1  }
}
td{ padding:8px 12px; font-size:13px; vertical-align:middle }
.td-num{
  font-family:var(--mono); font-size:10px;
  color:var(--muted); width:30px;
}
/* Kolom UID — disembunyikan di mobile via media query */
.col-uid{ white-space:nowrap }
/* Kolom tombol — lebar pas konten, tidak stretch */
.col-act,.col-del{ width:1%; white-space:nowrap }
.uid-tag{
  font-family:var(--mono); font-size:10px;
  color:var(--blue); background:#3eb8ff0d;
  border:1px solid #3eb8ff25;
  padding:2px 6px; display:inline-block;
  white-space:nowrap;
}
.ts-cell{
  font-family:var(--mono); font-size:10px;
  color:var(--muted); white-space:nowrap;
}

/* ── INLINE EDIT ─────────────────────────────────────── */
.edit-wrap{
  display:flex; gap:6px; align-items:center;
}
.name-inp{
  font-family:var(--ui); font-size:13px;
  background:#0a0a12; border:1px solid var(--border2);
  color:var(--text); padding:4px 8px;
  /* flex-grow agar isi ruang yang ada, min-width 0 */
  flex:1 1 80px; min-width:0;
  outline:none; transition:border-color .2s;
}
.name-inp:focus{ border-color:var(--amber) }

/* ── LIVE BADGE ─────────────────────────────────────── */
.live-badge{
  display:inline-flex; align-items:center; gap:5px;
  font-family:var(--mono); font-size:9px; color:var(--green);
  border:1px solid #00e87a30; padding:2px 7px; letter-spacing:1px;
  white-space:nowrap;
}
.live-dot{
  width:5px; height:5px; border-radius:50%; flex-shrink:0;
  background:var(--green); animation:ping 1.8s ease-in-out infinite;
}

/* ── EMPTY STATE ────────────────────────────────────── */
.empty-row td{
  text-align:center; padding:32px 16px;
  font-family:var(--mono); font-size:11px;
  color:var(--muted); letter-spacing:1px;
}

/* ── TOAST ──────────────────────────────────────────── */
#toast{
  position:fixed; bottom:16px;
  right:16px; left:16px;      /* full-width di mobile */
  font-family:var(--mono); font-size:11px; letter-spacing:1px;
  padding:10px 16px; border:1px solid;
  background:var(--panel);
  opacity:0; transform:translateY(8px);
  transition:opacity .2s, transform .2s;
  pointer-events:none; z-index:9999;
}
#toast.show{ opacity:1; transform:translateY(0) }
#toast.ok  { color:var(--green); border-color:var(--green) }
#toast.err { color:var(--red);   border-color:var(--red)   }
/* desktop: toast hanya sekebar konten, rata kanan */
@media(min-width:521px){
  #toast{ left:auto; max-width:320px }
}

/* ════════════════════════════════════════════════════════
   RESPONSIVE BREAKPOINTS
   520px  = kebanyakan HP android/iPhone standar
   400px  = HP kecil / landscape sempit
   ════════════════════════════════════════════════════════ */

/* ── ≤ 520px : HP standar ───────────────────────────── */
@media(max-width:520px){
  /* Header — hapus uptime, logo-sub; overflow visible  */
  header        { overflow:visible; padding:0 12px }
  .uptime-box   { display:none }
  .logo-sub     { display:none }
  .logo-text    { font-size:11px; letter-spacing:1px }

  /* Main padding lebih kecil */
  main{ padding:12px 10px 80px }

  /* Stat — font turun agar angka 3-digit tidak overflow */
  .stat-val  { font-size:20px }
  .stat-card { padding:11px 10px }
  .stat-lbl  { font-size:7px; letter-spacing:1px }

  /* Section header — tombol bisa wrap ke bawah */
  .sec-head{ gap:6px }

  /* Tabel user — sembunyikan kolom UID dan tombol-teks */
  .col-uid            { display:none }
  /* Tabel log — sembunyikan kolom UID (#3) */
  #lTbody tr td:nth-child(3),
  #lTbody ~ * th:nth-child(3){ display:none }

  /* Cell padding lebih kecil */
  td        { padding:7px 8px; font-size:12px }
  thead th  { padding:8px 8px }

  /* Input nama: isi sisa lebar kolom sepenuhnya */
  .name-inp { width:100%; flex-basis:0 }

  /* Tombol lebih kompak */
  .btn{ font-size:10px; padding:4px 9px; letter-spacing:.5px }
}

/* ── ≤ 400px : HP kecil ─────────────────────────────── */
@media(max-width:400px){
  header{ padding:0 8px; gap:6px }
  .logo-icon{ width:24px; height:24px }
  .logo-icon svg{ width:13px; height:13px }
  .ws-pill  { padding:3px 6px; font-size:8px }

  main{ padding:10px 6px 80px }

  /* Stat — 3 kolom tapi lebih mungil */
  .stat-grid{ gap:5px }
  .stat-val { font-size:17px }
  .stat-card{ padding:9px 8px }

  /* Tabel */
  td       { padding:6px 6px; font-size:11px }
  thead th { padding:7px 6px }
  .td-num  { width:24px }

  /* Tombol */
  .btn{ font-size:9px; padding:4px 7px }
}
</style>
</head>
<body>

<header>
  <div class="logo-wrap">
    <div class="logo-icon">
      <svg viewBox="0 0 16 16"><path d="M2 4h12v8H2zM5 4V2h6v2M4 7h2v2H4zM8 7h4v1H8M8 9h3v1H8"/></svg>
    </div>
    <div>
      <div class="logo-text">ABSENSI</div>
      <div class="logo-sub">ESP32 · RFID · PANEL</div>
    </div>
  </div>
  <div class="header-right">
    <div class="ws-pill" id="wsPill">
      <div class="ws-dot" id="wsDot"></div>
      <span id="wsLabel">OFFLINE</span>
    </div>
    <div class="uptime-box" id="uptime">--:--:--</div>
  </div>
</header>

<!-- TICKER -->
<div class="ticker">
  <div class="ticker-label">LIVE</div>
  <div class="ticker-track">
    <div class="ticker-inner" id="tickerInner">
      <span>SISTEM ABSENSI ESP32</span>
      <span class="ticker-sep">◆</span>
      <span>WEBSOCKET REALTIME</span>
      <span class="ticker-sep">◆</span>
      <span>LITTLEFS STORAGE</span>
      <span class="ticker-sep">◆</span>
      <span>TAP KARTU RFID UNTUK ABSEN</span>
      <span class="ticker-sep">◆</span>
      <span>SISTEM ABSENSI ESP32</span>
      <span class="ticker-sep">◆</span>
      <span>WEBSOCKET REALTIME</span>
      <span class="ticker-sep">◆</span>
      <span>LITTLEFS STORAGE</span>
      <span class="ticker-sep">◆</span>
      <span>TAP KARTU RFID UNTUK ABSEN</span>
      <span class="ticker-sep">◆</span>
    </div>
  </div>
</div>

<main>

  <!-- STAT GRID -->
  <div class="stat-grid">
    <div class="stat-card">
      <div class="stat-lbl">Total User</div>
      <div class="stat-val amber" id="sU">0</div>
      <div class="stat-bar"><div class="stat-bar-fill" id="sUBar" style="width:0%"></div></div>
    </div>
    <div class="stat-card">
      <div class="stat-lbl">Total Absen</div>
      <div class="stat-val green" id="sL">0</div>
      <div class="stat-bar"><div class="stat-bar-fill" style="background:var(--green);width:0%" id="sLBar"></div></div>
    </div>
    <div class="stat-card">
      <div class="stat-lbl">Slot Tersisa</div>
      <div class="stat-val blue" id="sF">50</div>
      <div class="stat-bar"><div class="stat-bar-fill" style="background:var(--blue)" id="sFBar"></div></div>
    </div>
  </div>

  <!-- USER TABLE -->
  <div class="section">
    <div class="sec-head">
      <div class="sec-title">
        <div class="sec-title-bar"></div>
        Daftar User
      </div>
      <div class="sec-actions">
        <button class="btn btn-amber" onclick="reqUsers()">↺ REFRESH</button>
      </div>
    </div>
    <div class="tbl-wrap">
      <table>
        <thead>
          <tr>
            <th class="td-num">#</th>
            <th>NAMA</th>
            <th class="col-uid">UID</th>
            <th class="col-act"></th>
            <th class="col-del"></th>
          </tr>
        </thead>
        <tbody id="uTbody">
          <tr class="empty-row"><td colspan="4">// menunggu koneksi...</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <!-- LOG TABLE -->
  <div class="section">
    <div class="sec-head">
      <div class="sec-title">
        <div class="sec-title-bar" style="background:var(--green)"></div>
        Log Absensi
        <span class="live-badge"><span class="live-dot"></span>LIVE</span>
      </div>
      <div class="sec-actions">
        <button class="btn btn-ghost" onclick="clearLogView()">✕ CLEAR VIEW</button>
        <button class="btn btn-amber" onclick="location='/api/logs/csv'">↓ CSV</button>
      </div>
    </div>
    <div class="tbl-wrap">
      <table>
        <thead>
          <tr>
            <th class="td-num">#</th>
            <th>NAMA</th>
            <th>UID</th>
            <th>WAKTU</th>
          </tr>
        </thead>
        <tbody id="lTbody">
          <tr class="empty-row"><td colspan="4">// belum ada log absensi</td></tr>
        </tbody>
      </table>
    </div>
  </div>

</main>

<div id="toast"></div>

<script>
'use strict';
const HOST = location.hostname;
let ws, reconnTmr;
let users = [], logs = [], logViewOffset = 0;

// ── ESC ──────────────────────────────────────────────
function esc(s){
  return String(s)
    .replace(/&/g,'&amp;')
    .replace(/</g,'&lt;')
    .replace(/>/g,'&gt;')
    .replace(/"/g,'&quot;');
}

// ── TOAST ────────────────────────────────────────────
let toastTmr;
function toast(msg, ok=true){
  const el = document.getElementById('toast');
  el.textContent = (ok ? '✓ ' : '✗ ') + msg.toUpperCase();
  el.className = 'show ' + (ok ? 'ok' : 'err');
  clearTimeout(toastTmr);
  toastTmr = setTimeout(()=>{ el.classList.remove('show') }, 2800);
}

// ── STATS ────────────────────────────────────────────
function updateStats(){
  const u = users.length, l = logs.length, f = 50 - u;
  document.getElementById('sU').textContent = u;
  document.getElementById('sL').textContent = l;
  document.getElementById('sF').textContent = f;
  document.getElementById('sUBar').style.width = (u/50*100)+'%';
  document.getElementById('sLBar').style.width = Math.min(l/20*100,100)+'%';
  document.getElementById('sFBar').style.width = (f/50*100)+'%';
  // Update ticker dengan last absen
  if(logs.length){
    const last = logs[logs.length-1];
    updateTicker(`LAST: ${last.name} — ${last.time}`);
  }
}

// ── TICKER UPDATE ─────────────────────────────────────
function updateTicker(extra){
  // hanya tambah extra info di depan tanpa ganti seluruh ticker
}

// ── RENDER USERS ─────────────────────────────────────
function renderUsers(){
  const tb = document.getElementById('uTbody');
  if(!users.length){
    tb.innerHTML='<tr class="empty-row"><td colspan="4">// belum ada user terdaftar</td></tr>';
    updateStats(); return;
  }
  tb.innerHTML = users.map((u,i)=>`
    <tr>
      <td class="td-num">${String(i+1).padStart(2,'0')}</td>
      <td><input class="name-inp" id="nm${i}" value="${esc(u.name)}" maxlength="19" spellcheck="false"></td>
      <td class="col-uid"><span class="uid-tag">${esc(u.uid)}</span></td>
      <td class="col-act"><button class="btn btn-green" onclick="saveName(${i})">✓</button></td>
      <td class="col-del"><button class="btn btn-red"   onclick="delUser(${i},'${esc(u.name)}')">✕</button></td>
    </tr>`).join('');
  updateStats();
}

// ── RENDER LOGS ───────────────────────────────────────
function renderLogs(flashFirst=false){
  const tb = document.getElementById('lTbody');
  const visible = [...logs].reverse().slice(logViewOffset);
  if(!visible.length){
    tb.innerHTML='<tr class="empty-row"><td colspan="4">// belum ada log absensi</td></tr>';
    updateStats(); return;
  }
  tb.innerHTML = visible.map((l,i)=>`
    <tr id="lr${i}" ${i===0&&flashFirst?'class="new-row"':''}>
      <td class="td-num">${String(logs.length - i).padStart(3,'0')}</td>
      <td style="font-weight:600;letter-spacing:.5px">${esc(l.name)}</td>
      <td><span class="uid-tag">${esc(l.uid)}</span></td>
      <td class="ts-cell">${esc(l.time)}</td>
    </tr>`).join('');
  updateStats();
}

function clearLogView(){
  logViewOffset = logs.length;
  renderLogs();
  toast('View log dibersihkan');
}

// ── WEBSOCKET ─────────────────────────────────────────
function connectWS(){
  clearTimeout(reconnTmr);
  ws = new WebSocket('ws://'+HOST+':81');

  ws.onopen = ()=>{
    const pill = document.getElementById('wsPill');
    pill.classList.add('on');
    document.getElementById('wsLabel').textContent = 'ONLINE';
    ws.send(JSON.stringify({cmd:'init'}));
  };

  ws.onclose = ()=>{
    const pill = document.getElementById('wsPill');
    pill.classList.remove('on');
    document.getElementById('wsLabel').textContent = 'OFFLINE';
    reconnTmr = setTimeout(connectWS, 3000);
  };

  ws.onerror = ()=> ws.close();

  ws.onmessage = (e)=>{
    let d; try{ d=JSON.parse(e.data) }catch{ return }

    if(d.type==='status'){
      document.getElementById('uptime').textContent = d.uptime;
      // update stat counts tanpa re-render tabel
      document.getElementById('sU').textContent = d.users;
      document.getElementById('sF').textContent = 50 - d.users;
    }
    else if(d.type==='users'){
      users = d.users||[];
      renderUsers();
    }
    else if(d.type==='logs'){
      logs = d.logs||[];
      renderLogs();
    }
    else if(d.type==='attend'){
      logs.push({name:d.name, uid:d.uid, time:d.time});
      renderLogs(true);
      toast(d.name+' ABSEN', true);
    }
    else if(d.type==='userchange'){
      users = d.users||[];
      renderUsers();
      if(d.msg) toast(d.msg, true);
    }
  };
}

// ── WS COMMANDS ──────────────────────────────────────
function reqUsers(){ if(ws&&ws.readyState===1) ws.send(JSON.stringify({cmd:'getUsers'})) }

// ── HTTP ACTIONS ──────────────────────────────────────
async function saveName(idx){
  const nm = document.getElementById('nm'+idx).value.trim();
  if(!nm){ toast('NAMA TIDAK BOLEH KOSONG',false); return }
  try{
    const r = await fetch('/api/rename',{
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:`idx=${idx}&name=${encodeURIComponent(nm)}`
    });
    const d = await r.json();
    if(d.ok) toast('NAMA DISIMPAN');
    else     toast('GAGAL SIMPAN NAMA',false);
  }catch{ toast('KONEKSI GAGAL',false) }
}

async function delUser(idx, name){
  if(!confirm(`Hapus user "${name}"?`)) return;
  try{
    const r = await fetch('/api/delete',{
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:`idx=${idx}`
    });
    const d = await r.json();
    if(d.ok) toast(name+' DIHAPUS');
    else     toast('GAGAL HAPUS',false);
  }catch{ toast('KONEKSI GAGAL',false) }
}

// ── INIT ─────────────────────────────────────────────
connectWS();
</script>
</body>
</html>
)rawhtml";
