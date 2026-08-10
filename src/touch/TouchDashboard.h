#pragma once

#include <pgmspace.h>

// Adapted from iHeater-Remote's RemoteDashboard.h — same visual language (dark
// rail nav, cards, metric tiles) restructured for the dryer.
//
// The important difference: iHeater's dashboard could only report the command it
// had sent, because that board has no sensors. The dryer reports real readings —
// air temperature, humidity, heater duty and fan state come back over UART from
// the RP2040, per unit. So the home view shows measured values, not just intent.
//
// The MENU tab renders whatever the controller declares. Nothing about the tree
// is hardcoded here: /api/menu returns metadata (title, type, min/max/step) plus
// live values, so a controller firmware change shows up without touching this UI.

namespace idryer_touch {

static const char kTouchDashboardHtml[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" type="image/png" href="/logo.png"><title>iDryer Touch</title>
<style>
:root{color-scheme:dark;--bg:#090d14;--panel:#101722;--panel2:#151f2d;--line:#29384b;--text:#eaf1fa;--muted:#91a2b8;--accent:#5ba9ff;--active:#183d62;--danger:#9c3442;--warm:#ffae57;font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 90% 0,#122742 0,var(--bg) 46%);color:var(--text);min-height:100vh}
.shell{display:flex;min-height:100vh}
.rail{width:76px;padding:12px 8px;display:flex;flex-direction:column;align-items:center;gap:8px;border-right:1px solid var(--line);background:#0b111a}
.brand{width:44px;height:44px;border-radius:10px;object-fit:cover;flex:none}
/* Dot only. The rail is 76px and "Reconnecting" does not fit at any usable size,
   horizontally or rotated — the word lives in the topline instead. The label
   stays in the DOM, visually hidden, so screen readers still announce it. */
.connection{display:flex;align-items:center;justify-content:center;margin:9px 0 11px}
.connection span{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}
.dot{width:7px;height:7px;border-radius:50%;background:#68778a;flex:none}.dot.online{background:#4bd192;box-shadow:0 0 10px #4bd192}
.tab{width:44px;height:44px;border:1px solid transparent;border-radius:9px;background:transparent;color:var(--muted);cursor:pointer;text-decoration:none;display:grid;place-items:center;position:relative;line-height:0;font-size:0}
.tab:hover,.tab[aria-selected=true]{background:var(--active);border-color:#376493;color:#fff}
/* Pictographic tabs, as in iHeater-Remote: the label stays in the DOM for
   screen readers (font-size:0 hides it visually) and ::before paints an SVG
   mask filled with currentColor, so the icon tracks the selected/hover state.
   Word labels do not fit a 44px box — "STORE" overflows its own highlight. */
.tab::before{content:"";position:absolute;inset:0;margin:auto;display:block;width:23px;height:23px;background:currentColor}
.tab[data-page=home]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m3 10 9-7 9 7'/%3E%3Cpath d='M5 9v11h14V9'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m3 10 9-7 9 7'/%3E%3Cpath d='M5 9v11h14V9'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=presets]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='12' r='8'/%3E%3Ccircle cx='12' cy='12' r='2.5'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Ccircle cx='12' cy='12' r='8'/%3E%3Ccircle cx='12' cy='12' r='2.5'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=dry]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 2c3 3 3 6 0 9'/%3E%3Cpath d='M7 5c3 3 3 7 0 11'/%3E%3Cpath d='M17 5c3 3 3 7 0 11'/%3E%3Cpath d='M4 19c4 2 12 2 16 0'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M12 2c3 3 3 6 0 9'/%3E%3Cpath d='M7 5c3 3 3 7 0 11'/%3E%3Cpath d='M17 5c3 3 3 7 0 11'/%3E%3Cpath d='M4 19c4 2 12 2 16 0'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=store]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Crect x='3' y='4' width='18' height='4' rx='1'/%3E%3Cpath d='M5 8v11h14V8'/%3E%3Cpath d='M10 12h4'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Crect x='3' y='4' width='18' height='4' rx='1'/%3E%3Cpath d='M5 8v11h14V8'/%3E%3Cpath d='M10 12h4'/%3E%3C/svg%3E") center/contain no-repeat}
.tab[data-page=menu]::before{-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M4 7h10'/%3E%3Cpath d='M4 12h16'/%3E%3Cpath d='M4 17h7'/%3E%3Ccircle cx='18' cy='7' r='2'/%3E%3Ccircle cx='15' cy='17' r='2'/%3E%3C/svg%3E") center/contain no-repeat;mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='black' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M4 7h10'/%3E%3Cpath d='M4 12h16'/%3E%3Cpath d='M4 17h7'/%3E%3Ccircle cx='18' cy='7' r='2'/%3E%3Ccircle cx='15' cy='17' r='2'/%3E%3C/svg%3E") center/contain no-repeat}
.tab.settings{margin-top:auto;font-size:25px;line-height:1}.tab.settings::before{display:none}
/* margin:0 auto, not margin:auto — the shell is a flex row, and an auto margin
   on the cross axis beats align-items, which vertically centred the whole page
   whenever the content was shorter than the viewport. */
.main{flex:1;min-width:0;padding:22px;max-width:1200px;margin:0 auto}
.topline{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin-bottom:18px}
.topline h1{font-size:22px;letter-spacing:-.03em;margin:0}.topline p{margin:0;color:var(--muted);font-size:13px}
.units{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px}
.card,.page{background:color-mix(in srgb,var(--panel) 94%,transparent);border:1px solid var(--line);border-radius:12px;padding:16px}
/* Unit cards stretch to equal height, so the trailing action has to be pinned
   to the bottom — otherwise a card with a progress bar pushes its button lower
   than its neighbours' and the row of Stop buttons looks misaligned. */
.units .card{display:flex;flex-direction:column}
.card h2,.page h2{font-size:12px;text-transform:uppercase;letter-spacing:.09em;color:var(--muted);margin:0 0 13px;display:flex;justify-content:space-between;align-items:center}
.state{display:flex;align-items:center;gap:10px;font-size:25px;font-weight:750}
.state i{width:10px;height:10px;border-radius:50%;background:#627287;flex:none}
.card[data-active=true] .state i{background:var(--warm);box-shadow:0 0 14px var(--warm)}
.details{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px;margin-top:16px}
.metric{padding:11px;border:1px solid #233247;border-radius:8px;background:#0d141e}
.metric span{display:block;font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.07em}
.metric strong{display:block;font-size:20px;margin-top:3px}
.bar{height:6px;border-radius:3px;background:#0d141e;border:1px solid #233247;margin-top:14px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--accent);width:0}
.actions,.preset-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}
/* Must follow .actions — same specificity, so source order decides. Earlier it
   lost and the Stop button rendered a quarter-width. */
.unit-actions{margin-top:auto;padding-top:14px;grid-template-columns:1fr}
/* `font:700 14px inherit` (as in iHeater's CSS) is an invalid shorthand —
   `inherit` is not a legal font-family there, so the whole declaration is
   dropped and buttons fall back to the UA's 13.33px. Set the parts instead. */
.btn{min-height:45px;border:1px solid #38506b;border-radius:8px;background:#172537;color:var(--text);font-family:inherit;font-size:14px;font-weight:700;cursor:pointer;padding:7px}
.btn:hover,.btn.active{background:var(--active);border-color:var(--accent)}
.btn.primary{background:#1d5d99;border-color:#69b4ff}.btn.stop{background:#4b202a;border-color:#844150}
.btn.sm{min-height:34px;font-size:12px;padding:4px 10px}
/* Faults sit outside the tab pages so a latched error is visible from any tab —
   the controller keeps re-posting until cleared, and it vents the servo while
   it does, so this is not something to hide one click deep. */
.faults{border:1px solid var(--danger);background:#1a0f14;border-radius:10px;padding:10px 12px;margin:0 0 14px}
.faults .fhead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:6px}
.faults ul{list-style:none;margin:0;padding:0;font-size:13px}
.faults li{display:flex;gap:8px;align-items:baseline;padding:3px 0;border-top:1px solid #2a1a20}
.faults li:first-child{border-top:0}
.faults .sev{flex:none;font-size:11px;font-weight:700;letter-spacing:.04em;color:var(--warm)}
.faults .sev.crit{color:#ff7d8f}
.faults .src{flex:none;color:var(--text);font-weight:600}
.faults .msg{color:var(--muted)}
.faults .age{margin-left:auto;flex:none;color:var(--muted);font-size:11px;white-space:nowrap}
.page{display:none;max-width:720px}.page.active{display:block}
/* Home is a bare container for the unit cards — without this it renders as a
   narrow card wrapping more cards, which double-borders and cramps the grid. */
.page[data-content=home]{background:none;border:0;padding:0;max-width:none}
.page p,.note{color:var(--muted);font-size:13px;line-height:1.5}
label{display:grid;gap:7px;font-size:12px;color:var(--muted);margin:13px 0}
input,select{width:100%;min-height:43px;background:#0c131d;border:1px solid #38506b;border-radius:8px;color:var(--text);font:inherit;padding:0 11px}
.feedback{min-height:18px;color:var(--muted);font-size:13px;margin-top:10px}
.footer{font-size:11px;color:var(--muted);margin:18px 0 0}.footer a{color:inherit;text-decoration:none}.footer a:hover{color:var(--accent)}
/* Menu rows read as a list you walk, not a form: submenus are full-width
   targets with a chevron, leaves keep their control on the right. */
.mlist{border:1px solid var(--line);border-radius:10px;overflow:hidden;background:var(--panel2)}
.mrow{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:center;padding:10px 12px;border-bottom:1px solid #1b2634;min-height:52px}
.mrow:last-child{border-bottom:0}
.mrow .lbl{min-width:0}
.mrow .lbl b{display:block;font-weight:600;font-size:14px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.mrow .lbl small{display:block;color:var(--muted);font-size:11px;margin-top:2px}
.mrow.sub{cursor:pointer}
.mrow.sub:hover{background:#132033}
.mrow .chev{color:var(--muted);font-size:17px;line-height:1}
.mrow .val{display:flex;align-items:center;gap:7px}
.mrow .val input{width:96px;min-height:36px;text-align:right}
.mrow .val .u{color:var(--muted);font-size:12px;min-width:26px}
.sw{width:52px;min-height:30px;border-radius:15px;border:1px solid #38506b;background:#0c131d;position:relative;cursor:pointer;padding:0}
.sw::after{content:"";position:absolute;top:2px;left:2px;width:24px;height:24px;border-radius:50%;background:var(--muted);transition:transform .12s,background .12s}
.sw[aria-pressed=true]{background:var(--active);border-color:#69b4ff}
.sw[aria-pressed=true]::after{transform:translateX(22px);background:var(--accent)}
.mempty{color:var(--muted);font-size:13px;padding:14px 12px}
.useg{display:flex;gap:6px;margin:6px 0 2px}
.useg button{flex:1;min-height:42px;border:1px solid #38506b;border-radius:8px;background:#172537;
  color:var(--muted);font:700 13px inherit;cursor:pointer}
.useg button[aria-pressed=true]{background:var(--primary,#1d5d99);border-color:#69b4ff;color:#fff}
.useg.one{display:none}
.preset-cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:10px;margin-top:12px}
.pc{background:var(--panel2);border:1px solid var(--edge,#233247);border-radius:9px;padding:11px}
.pc b{display:block;font-size:15px;margin-bottom:8px}
.pc .f{display:grid;grid-template-columns:auto 1fr;gap:6px;align-items:center;margin:5px 0;font-size:11px;color:var(--muted)}
.pc .f input{width:100%;min-height:34px;text-align:right}
.pc .btn{width:100%;margin-top:9px;min-height:38px}
.crumb{display:flex;gap:4px;align-items:center;flex-wrap:wrap;margin:0 0 12px;font-size:12px;color:var(--muted)}
.crumb button{background:none;border:0;color:var(--accent);cursor:pointer;font:inherit;padding:2px 4px;border-radius:5px}
.crumb button:hover{background:var(--active)}
.crumb i{font-style:normal;opacity:.5}
.crumb b{color:var(--text);font-weight:600;padding:2px 4px}
.hidden{display:none}
/* The rail carries four tabs plus the settings gear. Spelling out the
   connection state as well overflows a 375px viewport, so on narrow screens the
   status dot alone carries it. */
@media(max-width:650px){.shell{display:block}.rail{position:sticky;top:0;z-index:2;width:100%;height:58px;padding:7px 6px;gap:4px;flex-direction:row;align-items:center;border-right:0;border-bottom:1px solid var(--line)}.brand{width:38px;height:38px;border-radius:9px}.connection{margin:0;flex:none}.tab{width:42px;min-width:42px;flex:none;height:40px}.tab::before{width:21px;height:21px}.tab.settings{margin:0 0 0 auto;font-size:22px}.main{padding:15px}.units{grid-template-columns:1fr}.actions,.preset-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.topline p{display:none}}
</style></head>
<body><div class="app" id="app"><div class="shell">
<nav class="rail" aria-label="Navigation">
<img class="brand" src="/logo.png" alt="iDryer" width="44" height="44">
<div class="connection"><i class="dot" id="dot"></i><span id="conn">Connecting</span></div>
<button class="tab" data-page="home" aria-selected="true" title="Dashboard">HOME</button>
<button class="tab" data-page="presets" title="Material presets">MAT</button>
<button class="tab" data-page="dry" title="Manual drying">DRY</button>
<button class="tab" data-page="store" title="Storage">STORE</button>
<button class="tab" data-page="menu" title="Controller menu">MENU</button>
<a class="tab settings" href="/setup" aria-label="Settings" title="Settings">&#9881;</a>
</nav>
<main class="main">
<div class="topline"><h1>iDryer Touch</h1><p id="network">Loading device state…</p></div>

<div class="faults" id="faults" hidden>
<div class="fhead"><strong id="faultsTitle">Faults</strong><button class="btn sm" id="clearFaults">Clear faults</button></div>
<ul id="faultsList"></ul>
</div>

<section class="page active" data-content="home">
<div class="units" id="units"></div>
<p class="footer"><a href="https://github.com/justinh-rahb/iDryer-Touch" target="_blank" rel="noopener">iDryer Touch <span id="version">—</span></a> · <span id="mcu">controller not detected</span></p>
</section>

<section class="page" data-content="presets"><h2>Material presets <button class="btn sm" id="presetReload">Refresh</button></h2>
<p>Read from the controller's own preset menu. Editing a temperature or time writes it back, so the MY1–MY3 slots are yours to define.</p>
<label>Unit<div class="useg" id="presetUnit" role="group"></div></label>
<div id="presetGrid" class="preset-cards"></div>
<div class="feedback" id="presetFeedback"></div></section>

<section class="page" data-content="dry"><h2>Start drying</h2>
<p>Runs the heater at a target temperature for a set time, then stops. Values are sent straight to the controller.</p>
<label>Unit<div class="useg" id="dryUnit" role="group"></div></label>
<label>Temperature (°C)<input id="dryTemp" type="number" min="30" max="110" inputmode="numeric"></label>
<label>Duration (minutes)<input id="dryTime" type="number" min="1" max="1440" inputmode="numeric"></label>
<div class="actions"><button class="btn primary" id="startDry">Start drying</button><button class="btn stop" id="stopDry">Stop</button></div>
<div class="feedback" id="dryFeedback"></div></section>

<section class="page" data-content="store"><h2>Storage mode</h2>
<p>Holds the chamber below a humidity target indefinitely, heating only as needed.</p>
<label>Unit<div class="useg" id="storeUnit" role="group"></div></label>
<label>Temperature (°C)<input id="storeTemp" type="number" min="30" max="110" inputmode="numeric"></label>
<label>Humidity target (%)<input id="storeHum" type="number" min="1" max="90" inputmode="numeric"></label>
<div class="actions"><button class="btn primary" id="startStore">Start storage</button><button class="btn stop" id="stopStore">Stop</button></div>
<div class="feedback" id="storeFeedback"></div></section>

<section class="page" data-content="menu"><h2>Controller menu <button class="btn sm" id="menuReload">Refresh</button></h2>
<p>Read live from the controller. Editing a value writes it back over UART, the same path the jog wheel uses.</p>
<div class="crumb" id="crumb"></div>
<div id="menuList"></div>
<div class="feedback" id="menuFeedback"></div></section>

</main></div></div>
<script>
const $=id=>document.getElementById(id);
const MODES=["Idle","Drying","Storage","Profile","Fault"];
let state=null,menuItems=null,menuParent=0;

// Segmented unit picker. A dropdown hides the choice behind an interaction;
// with at most three units the options fit inline, and the control removes
// itself entirely on a single-unit machine.
const unitSel={};
function renderUnits(id){
  const el=$(id); if(!el||!state) return;
  const n=state.unitsCount||1;
  if(unitSel[id]===undefined||unitSel[id]>=n) unitSel[id]=0;
  el.classList.toggle("one",n<2);
  if(el.childElementCount!==n){
    el.innerHTML="";
    for(let i=0;i<n;i++){
      const b=document.createElement("button");
      b.type="button"; b.textContent="Unit "+(i+1);
      b.onclick=()=>{unitSel[id]=i;renderUnits(id)};
      el.appendChild(b);
    }
  }
  [...el.children].forEach((b,i)=>b.setAttribute("aria-pressed",i===unitSel[id]));
}
function unitOf(id){return unitSel[id]||0}

document.querySelectorAll(".tab[data-page]").forEach(b=>b.onclick=()=>showPage(b.dataset.page));
function showPage(p){document.querySelectorAll(".tab[data-page]").forEach(b=>b.setAttribute("aria-selected",b.dataset.page===p));document.querySelectorAll("[data-content]").forEach(s=>s.classList.toggle("active",s.dataset.content===p));if(p==="menu"&&!menuItems)loadMenu();if(p==="presets"&&!presets.length)loadPresets()}

async function post(path){const r=await fetch(path,{method:"POST"});const d=await r.json().catch(()=>({error:"Request failed"}));if(!r.ok)throw Error(d.error||"Request failed");return d}
function dur(s){if(!s)return"—";const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+"h "+String(m).padStart(2,"0")+"m":m+"m"}
// Both setpoints in one box: "what I asked for" is a single idea, and it keeps
// the run length visible without spending another metric tile.
function tgt(u){const t=Number(u.targetTempC).toFixed(0)+"°C";return u.durationS?t+" · "+dur(u.durationS):t}
function num(v,suffix,digits=1){return(v===undefined||v===null)?"—":Number(v).toFixed(digits)+suffix}

function unitCard(u,i){
  const active=u.mode!==0&&u.mode!==4;
  const pct=u.durationS?Math.min(100,100*u.elapsedS/u.durationS):0;
  return `<section class="card" data-active="${active}">
  <h2>Unit ${i+1}${u.mode===4?' <span style="color:var(--danger)">FAULT</span>':''}</h2>
  <div class="state"><i></i><span>${MODES[u.mode]||"Unknown"}</span></div>
  <div class="details">
    <div class="metric"><span>Air temp</span><strong>${num(u.airTempC,"°C")}</strong></div>
    <div class="metric"><span>Humidity</span><strong>${num(u.airHumidity,"%")}</strong></div>
    <div class="metric"><span>Target</span><strong>${active?tgt(u):"—"}</strong></div>
    <div class="metric"><span>Heater</span><strong>${num(u.heaterPower*100,"%",0)}</strong></div>
    <div class="metric"><span>Fan</span><strong>${u.fanOn?"On":"Off"}</strong></div>
    <div class="metric"><span>Elapsed</span><strong>${active?dur(u.elapsedS):"—"}</strong></div>
  </div>
  ${u.durationS?`<div class="bar"><i style="width:${pct}%"></i></div>`:""}
  <div class="actions unit-actions"><button class="btn stop" onclick="stopUnit(${i})">Stop unit ${i+1}</button></div>
</section>`}

async function stopUnit(i){try{await post("/api/command?do=stop&unit="+i)}catch(e){}}

function applyStatus(s){
  state=s;
  $("dot").classList.add("online");$("conn").textContent="Online";$("dot").title="Online";
  $("network").textContent=s.ssid+" · "+s.ip+" ("+s.wifiMode.toUpperCase()+")";
  $("version").textContent="v"+s.firmwareVersion;
  $("mcu").textContent=s.mcuConnected?("controller "+(s.mcuSerial||"connected")+" · menu rev "+s.menuRevision):"controller not detected";
  $("units").innerHTML=(s.units||[]).map(unitCard).join("");
  ["dryUnit","storeUnit","presetUnit"].forEach(renderUnits);
  syncFaults(s.errorCount|0);
}

// Only refetch when the count moves. applyStatus runs on every websocket push,
// and the fault text does not change between them — polling /api/errors at that
// rate would be a request every 500 ms for a list that is almost always empty.
let faultCount=-1;
async function syncFaults(n){
  if(n===faultCount) return;
  faultCount=n;
  const box=$("faults");
  if(!n){box.hidden=true;$("faultsList").innerHTML="";return}
  try{
    const d=await (await fetch("/api/errors")).json();
    const rows=(d.errors||[]).map(e=>{
      const crit=(e.sev||"").indexOf("CRIT")===0;
      const age=e.ageS<60?e.ageS+"s":Math.floor(e.ageS/60)+"m";
      // Fields are escaped server-side by htmlEscape(), same as the menu tree.
      return `<li><span class="sev${crit?" crit":""}">${e.sev}</span>`+
             `<span class="src">${e.src}</span>`+
             `<span class="msg">${e.msg} · unit ${e.unit}</span>`+
             `<span class="age">${age}</span></li>`;
    }).join("");
    $("faultsTitle").textContent=n===1?"1 fault reported":n+" faults reported";
    $("faultsList").innerHTML=rows;
    box.hidden=false;
  }catch(_){/* leave the previous list up rather than blanking on a blip */}
}

$("clearFaults").onclick=async()=>{
  try{
    await post("/api/command?do=clear_errors");
    faultCount=-1;               // force the next status push to re-read
    $("faults").hidden=true;
  }catch(e){$("faultsTitle").textContent="Could not clear: "+e.message}
};

$("startDry").onclick=async()=>{try{await post(`/api/command?do=drying&unit=${unitOf("dryUnit")}&temperature=${$("dryTemp").value}&duration=${$("dryTime").value}`);$("dryFeedback").textContent="Drying started."}catch(e){$("dryFeedback").textContent=e.message}};
$("stopDry").onclick=async()=>{try{await post("/api/command?do=stop&unit="+unitOf("dryUnit"));$("dryFeedback").textContent="Stopped."}catch(e){$("dryFeedback").textContent=e.message}};
$("startStore").onclick=async()=>{try{await post(`/api/command?do=storage&unit=${unitOf("storeUnit")}&temperature=${$("storeTemp").value}&humidity=${$("storeHum").value}`);$("storeFeedback").textContent="Storage started."}catch(e){$("storeFeedback").textContent=e.message}};
$("stopStore").onclick=async()=>{try{await post("/api/command?do=stop&unit="+unitOf("storeUnit"));$("storeFeedback").textContent="Stopped."}catch(e){$("storeFeedback").textContent=e.message}};

// Menu tree. Fetched in pages so the controller's ~26 KB tree never has to be
// assembled in one buffer on the ESP32 side.
let presets=[];
async function loadPresets(){
  $("presetFeedback").textContent="Loading…";
  try{
    const r=await fetch("/api/presets"); const d=await r.json();
    presets=d.presets||[]; $("presetFeedback").textContent="";
    renderPresets();
  }catch(e){$("presetFeedback").textContent="Could not load presets."}
}
function renderPresets(){
  renderUnits("presetUnit");
  // Values live in the cache, names in flash — between boot and the first
  // config the grid knows every material but none of their settings.
  if(state&&!state.menuRevision){$("presetGrid").innerHTML='<p class="note">Waiting for the controller to send its menu\u2026</p>';return}
  $("presetGrid").innerHTML=presets.map((p,i)=>`<div class="pc"><b>${p.name}</b>
    <div class="f"><span>Temp °C</span><input type="number" value="${p.temp}" onchange="savePreset(${i},'temp',this.value)"></div>
    <div class="f"><span>Time min</span><input type="number" value="${p.minutes}" onchange="savePreset(${i},'minutes',this.value)"></div>
    <button class="btn primary" onclick="dryPreset(${i})">Dry ${p.name}</button></div>`).join("")
    ||'<p class="note">No presets reported by the controller.</p>';
}
async function savePreset(i,field,val){
  const p=presets[i], id=field==="temp"?p.tempId:p.timeId;
  try{await post(`/api/set?id=${id}&unit=0&val=${val}`);p[field]=Number(val);$("presetFeedback").textContent=p.name+" updated."}
  catch(e){$("presetFeedback").textContent=e.message}
}
// Sends an ordinary drying command with an explicit unitId rather than invoking
// the preset's own START action — that action is global-scope, so the unit it
// would run on is the controller's choice.
async function dryPreset(i){
  const p=presets[i], u=unitOf("presetUnit")||0;
  try{await post(`/api/command?do=drying&unit=${u}&temperature=${p.temp}&duration=${p.minutes}`);
      $("presetFeedback").textContent=`${p.name} started on unit ${Number(u)+1}.`}
  catch(e){$("presetFeedback").textContent=e.message}
}
$("presetReload").onclick=loadPresets;

async function loadMenu(){
  $("menuFeedback").textContent="Loading…";
  try{
    let all=[],from=0,total=1;
    while(from<total){const r=await fetch(`/api/menu?from=${from}&count=48`);const d=await r.json();total=d.total;all=all.concat(d.items);from+=48}
    menuItems=all;$("menuFeedback").textContent="";renderMenu()
  }catch(e){$("menuFeedback").textContent="Could not load menu."}
}
$("menuReload").onclick=()=>{menuItems=null;menuParent=0;loadMenu()};

function renderMenu(){
  if(!menuItems)return;
  const kids=menuItems.filter(m=>m.p===menuParent&&m.id!==menuParent);

  // Breadcrumb: every ancestor clickable, current node plain.
  const path=[];let p=menuParent;
  while(p>=0){const m=menuItems.find(x=>x.id===p);if(!m)break;path.unshift(m);p=m.p}
  $("crumb").innerHTML=path.map((m,i)=>
    i===path.length-1?`<b>${m.n}</b>`
                     :`<button onclick="gotoMenu(${m.id})">${m.n}</button><i>›</i>`).join("")||"&nbsp;";

  if(!kids.length){$("menuList").innerHTML='<div class="mlist"><div class="mempty">Nothing here.</div></div>';return}

  $("menuList").innerHTML='<div class="mlist">'+kids.map(m=>{
    const per=m.g?` · per unit`:"";
    if(m.t===0){   // submenu
      const n=menuItems.filter(x=>x.p===m.id).length;
      return `<div class="mrow sub" onclick="gotoMenu(${m.id})">
        <div class="lbl"><b>${m.n}</b><small>${n} item${n===1?"":"s"}</small></div>
        <div class="chev">›</div></div>`;
    }
    if(m.t===1){   // action
      return `<div class="mrow"><div class="lbl"><b>${m.n}</b><small>action</small></div>
        <div class="val"><button class="btn sm" onclick="invokeItem(${m.id})">Run</button></div></div>`;
    }
    if(m.t===3){   // toggle
      return `<div class="mrow"><div class="lbl"><b>${m.n}</b><small>${m.val?"on":"off"}${per}</small></div>
        <div class="val"><button class="sw" aria-pressed="${!!m.val}"
             onclick="setItem(${m.id},${m.val?0:1})"></button></div></div>`;
    }
    return `<div class="mrow"><div class="lbl"><b>${m.n}</b>
        <small>${m.min}–${m.max}${m.step?` · step ${m.step}`:""}${per}</small></div>
      <div class="val"><input type="number" value="${m.val}" min="${m.min}" max="${m.max}"
           step="${m.step||1}" onchange="setItem(${m.id},this.value)">
        <span class="u">${m.u||""}</span></div></div>`;
  }).join("")+'</div>';
}

function gotoMenu(id){menuParent=id;renderMenu()}
async function setItem(id,val){try{await post(`/api/set?id=${id}&unit=0&val=${val}`);$("menuFeedback").textContent="Saved.";const m=menuItems.find(x=>x.id===id);if(m)m.val=Number(val);renderMenu()}catch(e){$("menuFeedback").textContent=e.message}}
async function invokeItem(id){try{await post(`/api/invoke?id=${id}`);$("menuFeedback").textContent="Sent."}catch(e){$("menuFeedback").textContent=e.message}}

let socket;
function connect(){
  socket=new WebSocket((location.protocol==="https:"?"wss":"ws")+"://"+location.hostname+":81/");
  socket.onmessage=e=>{try{applyStatus(JSON.parse(e.data))}catch(_){}};
  socket.onclose=()=>{$("dot").classList.remove("online");$("conn").textContent="Reconnecting";$("dot").title="Reconnecting";
    $("network").textContent="Reconnecting to device…";setTimeout(connect,1500)};
  socket.onerror=()=>socket.close();
}
connect();
</script></body></html>)HTML";

} // namespace idryer_touch
