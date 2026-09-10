// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/progmem.h"
namespace esphome::opengarage {
static const char GENERIC_SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenGarage setup</title>
<style>
body{font:17px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#f6f8fb;color:#182b38}
label{display:block;margin:1rem 0}input,select,button,textarea{box-sizing:border-box;font:inherit;width:100%;padding:.65rem;border:1px solid #789;border-radius:.35rem}
button{background:#176c86;color:white;cursor:pointer;margin:.35rem 0}button:disabled{opacity:.6;cursor:wait}.secondary{background:white;color:#07576c}
code{overflow-wrap:anywhere}#message{white-space:pre-wrap}small{display:block;color:#435765}a{color:#07576c}summary{cursor:pointer}details{margin:1.2rem 0}
[hidden]{display:none!important}textarea{font:14px monospace;min-height:13rem}h2{font-size:1.25rem}
</style>
<h1>OpenGarage setup</h1><p>Connect OpenGarage to your home Wi-Fi.</p>
<p id="loading">Loading device setup…</p>
<section id="paired" hidden><h2>Home Assistant pairing</h2><p>Paste this key when adding the discovered ESPHome device. Keep it private.</p>
<label>HA encryption key<input id="key" readonly spellcheck="false"></label><p>Security+ client ID: <code id="saved_id"></code>.</p><p><a href="/">Device controls and diagnostics</a></p>
<details><summary>Firmware update / return to stock</summary><p>This disables opener controls until a restart. Use only the matching OpenGarage application .bin.</p><button id="update" type="button">Enter firmware update mode</button><form id="upload" action="/update" method="post" enctype="multipart/form-data" hidden><label>Firmware .bin<input type="file" name="update" accept=".bin" required></label><button>Upload firmware</button></form></details></section>
<form id="setup" hidden autocomplete="off">
<label>Network name<select id="networks" required><option value="">Loading nearby networks…</option><option value="manual">Enter network name manually…</option></select></label>
<label id="manual_network" hidden>Network name (SSID)<input id="ssid" name="ssid" maxlength="32" spellcheck="false" disabled></label>
<button id="refresh" type="button" class="secondary">Refresh network list</button><small id="scan_status" role="status"></small>
<label>Wi-Fi password<input name="wifi_password" type="password" maxlength="63" autocomplete="off"></label>
<small>Use a 2.4 GHz network. Leave the password empty only for an open network.</small>
<details id="initial"><summary>Advanced — existing OpenGarage / identity import</summary>
<p>New setup generates an individual opener identity. When migrating from a personal ESPHome image, import your saved Security+ client ID to retain it.</p>
<label>Opener identity<select id="identity" name="identity"><option value="new">New setup (default)</option><option value="import">Import existing Security+ client ID</option></select></label>
<label id="import_field" hidden>Existing client ID<input id="client_id" name="client_id" maxlength="10" placeholder="Decimal or 0x hexadecimal"></label>
<p>If a previously configured device unexpectedly asks for setup, stop and recover your backup instead of creating a replacement identity.</p></details>
<button id="save">Save and connect</button>
<p id="setup_note"><small>Initial setup is available on this open AP for ten minutes after power-up. Use a trusted location; power-cycle to reopen it. Opener outputs stay disabled during setup.</small></p>
</form>
<section id="receipt" hidden><h2>Save your device credentials</h2>
<p>OpenGarage generated a unique admin password and HA key. Save the private details below before connecting: your phone's setup window may close when Wi-Fi changes.</p>
<label>Private recovery details<textarea id="credentials" readonly spellcheck="false"></textarea></label>
<button id="download" type="button" class="secondary">Download private details</button>
<button id="connect" type="button">I've saved these — connect</button><button id="back" type="button" class="secondary">Back to Wi-Fi settings</button>
<small id="receipt_note">These details are not saved to the device until you confirm. Do not share them in screenshots, logs or support reports.</small></section>
<p id="message" role="status"></p><small>Experimental firmware. Keep people clear of the opener. Setup never operates it.</small>
<script>
let info, pendingBody, networks=[];
const el=id=>document.getElementById(id), form=el('setup'), message=el('message');
function chooseNetwork(){
  const manual=el('networks').value==='manual';
  el('manual_network').hidden=!manual; el('ssid').disabled=!manual; el('ssid').required=manual;
}
function importChanged(){const importing=el('identity').value==='import';el('import_field').hidden=!importing;el('client_id').required=importing;}
async function refreshNetworks(){
  el('refresh').disabled=true;el('scan_status').textContent='Loading scanned networks…';
  const selected=el('networks').value.startsWith('ap')?networks[Number(el('networks').value.slice(2))]?.ssid:undefined;
  const manual=el('networks').value==='manual';
  try{
    const r=await fetch('/config.json',{cache:'no-store'});if(!r.ok)throw Error('Network list unavailable. You can enter the name manually.');
    const data=await r.json(), unique=new Map();
    for(const ap of data.aps||[]){if(!ap.ssid)continue;const old=unique.get(ap.ssid);if(!old||ap.rssi>old.rssi)unique.set(ap.ssid,ap);}
    networks=Array.from(unique.values()).sort((a,b)=>b.rssi-a.rssi);
    el('networks').replaceChildren(new Option(networks.length?'Choose a network…':'No networks found — enter manually',''));
    networks.forEach((ap,i)=>el('networks').add(new Option(ap.ssid+' — '+ap.rssi+' dBm · '+(ap.lock?'Secured':'Open'),'ap'+i)));
    el('networks').add(new Option('Enter network name manually…','manual'));
    const index=networks.findIndex(ap=>ap.ssid===selected);
    el('networks').value=manual?'manual':index>=0?'ap'+index:'';
    el('scan_status').textContent=networks.length+' network'+(networks.length===1?'':'s')+' found. Refresh for the latest ESPHome scan results.';
  }catch(e){el('scan_status').textContent=e.message;}
  el('refresh').disabled=false;chooseNetwork();
}
async function post(path,body){return fetch(path,{method:'POST',headers:{'X-OG-CSRF':info.token},body:new URLSearchParams(body)});}
(async()=>{try{
  const r=await fetch('/og/info',{cache:'no-store'});if(!r.ok)throw Error(await r.text());info=await r.json();
  el('loading').hidden=true;form.hidden=false;el('initial').hidden=info.configured;el('setup_note').hidden=info.configured;el('paired').hidden=!info.configured;
  el('key').value=info.key;el('saved_id').textContent=info.client_id;el('save').textContent=info.configured?'Save Wi-Fi and restart':'Save and connect';
  await refreshNetworks();
}catch(e){message.textContent=e.message;}})();
el('networks').addEventListener('change',chooseNetwork);el('refresh').addEventListener('click',refreshNetworks);el('identity').addEventListener('change',importChanged);
form.addEventListener('submit',async e=>{
  e.preventDefault();if(!info)return;el('save').disabled=true;message.textContent='';
  const data=new FormData(form);
  if(el('networks').value!=='manual'){const ap=el('networks').value.startsWith('ap')?networks[Number(el('networks').value.slice(2))]:undefined;if(!ap){message.textContent='Choose a network.';el('save').disabled=false;return;}data.set('ssid',ap.ssid);}
  pendingBody=new URLSearchParams(data).toString();
  try{
    const r=await post(info.configured?'/og/wifi':'/og/prepare',pendingBody);
    if(!r.ok){message.textContent=await r.text();el('save').disabled=false;return;}
    if(info.configured){message.textContent=await r.text();return;}
    const credentials=await r.json();
    el('credentials').value='OpenGarage private device details\nDevice: '+location.host+'\nUsername: admin\nAdmin / OTA / recovery AP password: '+credentials.admin+'\nHA encryption key: '+credentials.key+'\nSecurity+ client ID: '+credentials.client_id;
    form.hidden=true;el('receipt').hidden=false;el('connect').disabled=false;el('save').disabled=false;
  }catch(e){message.textContent=info.configured?'Connection changed. Check the device on your home Wi-Fi before retrying.':'Connection lost. Reload setup before trying again. No connection is started by the credential preview.';el('save').disabled=false;}
});
el('connect').addEventListener('click',async()=>{
  el('connect').disabled=true;el('back').disabled=true;
  try{const r=await post('/og/setup',pendingBody);message.textContent=await r.text();if(r.ok){el('receipt_note').textContent='Saved to the device. Keep these details private and rejoin your home Wi-Fi.';}else{el('connect').disabled=false;el('back').disabled=false;}}
  catch(e){message.textContent='Connection changed. Rejoin your Wi-Fi and check the device. Keep these credentials; do not repeat initial setup blindly.';}
});
el('back').addEventListener('click',()=>{el('receipt').hidden=true;form.hidden=false;message.textContent='';});
el('download').addEventListener('click',()=>{const url=URL.createObjectURL(new Blob([el('credentials').value+'\n'],{type:'text/plain'}));const a=document.createElement('a');a.href=url;a.download='opengarage-private-details.txt';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
el('update').addEventListener('click',async()=>{if(!info?.configured||!confirm('Disable opener controls until restart and enter firmware update mode?'))return;try{const r=await post('/og/update','');message.textContent=await r.text();el('upload').hidden=!r.ok;}catch(e){message.textContent=e.message;}});
</script></html>)HTML";
}  // namespace esphome::opengarage
