// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esphome/core/progmem.h"
namespace esphome::opengarage {
static const char GENERIC_SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenGarage Setup &amp; Update</title>
<style>
body{font:17px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#f6f8fb;color:#182b38}
label{display:block;margin:1rem 0}input,select,button,textarea{box-sizing:border-box;font:inherit;width:100%;padding:.65rem;border:1px solid #789;border-radius:.35rem}
button{background:#176c86;color:white;cursor:pointer;margin:.35rem 0}button:disabled{opacity:.6;cursor:wait}.secondary{background:white;color:#07576c}
code{overflow-wrap:anywhere}#message{white-space:pre-wrap}small{display:block;color:#435765}a{color:#07576c}
[hidden]{display:none!important}textarea{font:14px monospace;min-height:13rem}h2{font-size:1.25rem;margin:0 0 .75rem}
.setup-section{margin:1.75rem 0}.setup-section:not([hidden])~.setup-section:not([hidden]){border-top:1px solid #b6c5cf;padding-top:1.75rem}
progress{width:100%;height:1.2rem}
</style>
<h1>OpenGarage Setup &amp; Update</h1>
<p id="loading">Loading device setup…</p>
<section id="paired" class="setup-section" hidden><h2>Home Assistant Pairing</h2><p>Paste this key when adding the discovered ESPHome device. Keep it private.</p>
<label>HA encryption key<input id="key" readonly spellcheck="false"></label><p>Security+ 2.0 client ID: <code id="saved_id"></code>.</p><p id="dashboard_link" hidden><a href="/">Device controls and diagnostics</a></p>
</section>
<section id="firmware_settings" class="setup-section" hidden><h2>Firmware Update</h2><p>Update ESPHome or return to stock OpenGarage firmware. This disables opener controls until a restart. Use only an OpenGarage ESP8266 application .bin (4 MB hardware).</p><button id="update" type="button">Enter firmware update mode</button>
<form id="upload" action="/update" method="post" enctype="multipart/form-data" hidden><label>ESP8266 firmware .bin<input id="firmware" type="file" name="update" accept=".bin" required></label><button>Upload firmware</button>
<progress id="upload_progress" max="100" value="0" aria-label="Firmware upload progress" hidden></progress><p id="upload_status" role="status"></p></form></section>
<form id="setup" class="setup-section" hidden autocomplete="off"><h2>Change WiFi Settings</h2>
<label id="network_picker">Network name<select id="networks" required><option value="">Loading nearby networks…</option><option value="manual">Enter network name manually…</option></select></label>
<label id="manual_network" hidden>Network name (SSID)<input id="ssid" name="ssid" maxlength="32" spellcheck="false" disabled></label>
<button id="refresh" type="button" class="secondary">Refresh network list</button><small id="scan_status" role="status"></small>
<label>Wi-Fi password<input name="wifi_password" type="password" maxlength="63" autocomplete="off"></label>
<small>2.4 GHz only.</small>
<button id="save">Save and connect</button>
<p id="setup_note"><small>Setup stays open for 10 minutes.</small></p>
</form>
<section id="receipt" class="setup-section" hidden><h2>Save your device credentials</h2>
<p>Save these before continuing — your phone may drop this page when Wi-Fi switches over.</p>
<label>Private recovery details<textarea id="credentials" readonly spellcheck="false"></textarea></label>
<button id="download" type="button" class="secondary">Download private details</button>
<button id="connect" type="button">I've saved these — connect</button><button id="back" type="button" class="secondary">Back to Wi-Fi settings</button>
<small id="receipt_note">Don't share these in screenshots or logs.</small></section>
<p id="message" role="status"></p>
<p id="reconnect" hidden>Device address: <a id="local_address"></a><small>After restarting, join the same Wi-Fi network as the device. If this address doesn't open, check your router's connected-device list.</small></p>
<small>Experimental firmware — keep clear of the opener.</small>
<script>
let info, pendingBody, networks=[], scanAvailable=false, updateReady=false, uploadStarted=false, uploadActive=false;
const el=id=>document.getElementById(id), form=el('setup'), message=el('message');
function showReconnect(){el('reconnect').hidden=!el('local_address').getAttribute('href');}
function chooseNetwork(){
  const manual=!scanAvailable||el('networks').value==='manual';
  el('manual_network').hidden=!manual; el('ssid').disabled=!manual; el('ssid').required=manual;
}
async function refreshNetworks(){
  if(!scanAvailable)return;
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
    el('networks').value=manual?'manual':index>=0?'ap'+index:networks.length?'':'manual';
    el('scan_status').textContent=networks.length+' network'+(networks.length===1?'':'s')+' found. Refresh for the latest ESPHome scan results.';
  }catch(e){el('scan_status').textContent=e.message;el('networks').value='manual';}
  el('refresh').disabled=false;chooseNetwork();
}
async function post(path,body){return fetch(path,{method:'POST',headers:{'X-OG-CSRF':info.token},body:new URLSearchParams(body)});}
(async()=>{try{
  const r=await fetch('/og/info',{cache:'no-store'});if(!r.ok)throw Error(await r.text());info=await r.json();
  if(typeof info.hostname==='string'&&/^[a-z0-9][a-z0-9-]{0,62}\.local$/i.test(info.hostname)){
    el('local_address').setAttribute('href','http://'+info.hostname+'/');el('local_address').textContent='http://'+info.hostname+'/';
  }
  el('loading').hidden=true;form.hidden=false;el('setup_note').hidden=info.configured;el('paired').hidden=!info.configured;el('firmware_settings').hidden=!info.configured;
  el('dashboard_link').hidden=info.dashboard_available!==true;
  el('key').value=info.key;el('saved_id').textContent=info.client_id;el('save').textContent=info.configured?'Save Wi-Fi and restart':'Save and connect';
  // Initial setup is AP-only. Once configured, dashboard_available is false
  // precisely while the AP is active. Missing metadata defaults to manual.
  scanAvailable=info.configured===false||info.dashboard_available===false;
  el('network_picker').hidden=!scanAvailable;el('networks').disabled=!scanAvailable;el('networks').required=scanAvailable;
  el('refresh').hidden=!scanAvailable;el('scan_status').hidden=!scanAvailable;
  if(scanAvailable)await refreshNetworks();else chooseNetwork();
}catch(e){message.textContent=e.message;}})();
el('networks').addEventListener('change',chooseNetwork);el('refresh').addEventListener('click',refreshNetworks);
form.addEventListener('submit',async e=>{
  e.preventDefault();if(!info)return;el('save').disabled=true;message.textContent='';
  const data=new FormData(form);
  if(scanAvailable&&el('networks').value!=='manual'){const ap=el('networks').value.startsWith('ap')?networks[Number(el('networks').value.slice(2))]:undefined;if(!ap){message.textContent='Choose a network.';el('save').disabled=false;return;}data.set('ssid',ap.ssid);}
  pendingBody=new URLSearchParams(data).toString();
  try{
    const r=await post(info.configured?'/og/wifi':'/og/prepare',pendingBody);
    if(!r.ok){message.textContent=await r.text();el('save').disabled=false;return;}
    if(info.configured){message.textContent=await r.text();showReconnect();return;}
    const credentials=await r.json();
    el('credentials').value='OpenGarage private device details\nDevice: '+location.host+'\nUsername: admin\nAdmin / OTA / recovery AP password: '+credentials.admin+'\nHA encryption key: '+credentials.key+'\nSecurity+ 2.0 client ID: '+credentials.client_id;
    form.hidden=true;el('receipt').hidden=false;el('connect').disabled=false;el('save').disabled=false;
  }catch(e){message.textContent=info.configured?'Connection changed. Check the device on your new Wi-Fi before retrying; saving and connection are not confirmed.':'Connection lost. Reload setup before trying again. No connection is started by the credential preview.';if(info.configured)showReconnect();el('save').disabled=false;}
});
el('connect').addEventListener('click',async()=>{
  el('connect').disabled=true;el('back').disabled=true;
  try{const r=await post('/og/setup',pendingBody);message.textContent=await r.text();if(r.ok){showReconnect();el('receipt_note').textContent='Saved to the device. Keep these details private and rejoin your home Wi-Fi.';}else{el('connect').disabled=false;el('back').disabled=false;}}
  catch(e){showReconnect();message.textContent='Connection changed. Rejoin your Wi-Fi and check the device. Keep these credentials; do not repeat initial setup blindly.';}
});
el('back').addEventListener('click',()=>{el('receipt').hidden=true;form.hidden=false;message.textContent='';});
el('download').addEventListener('click',()=>{const url=URL.createObjectURL(new Blob([el('credentials').value+'\n'],{type:'text/plain'}));const a=document.createElement('a');a.href=url;const suffix=typeof info?.mac_suffix==='string'&&/^[a-f0-9]{4}$/i.test(info.mac_suffix)?'-'+info.mac_suffix.toLowerCase():'';a.download='opengarage-private-details'+suffix+'.txt';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
el('update').addEventListener('click',async()=>{if(uploadStarted||!info?.configured||!confirm('Disable opener controls until restart and enter firmware update mode?'))return;try{const r=await post('/og/update','');message.textContent=await r.text();updateReady=r.ok;el('upload').hidden=!r.ok;}catch(e){message.textContent=e.message;}});
window.addEventListener('beforeunload',e=>{if(uploadActive){e.preventDefault();e.returnValue='';}});
el('upload').addEventListener('submit',e=>{
  e.preventDefault();if(uploadStarted||!updateReady)return;
  const file=el('firmware').files[0], status=el('upload_status'), bar=el('upload_progress');
  if(!file||file.size===0){status.textContent='Choose a nonempty ESP8266 application .bin.';return;}
  const body=new FormData();body.append('update',file);
  uploadStarted=true;uploadActive=true;
  document.querySelectorAll('input,select,button').forEach(control=>{control.disabled=true;});
  el('dashboard_link').hidden=true;message.textContent='';bar.hidden=false;bar.value=0;
  status.textContent='Uploading: 0%. Keep this page open and do not remove power.';
  const unconfirmed=()=>{uploadActive=false;status.textContent='Update not confirmed: connection lost or timed out. Wait for the device and check its firmware version before retrying. Do not interrupt power during an update.';};
  try{
    const xhr=new XMLHttpRequest();
    xhr.upload.addEventListener('progress',event=>{
      if(!event.lengthComputable||event.total<=0){bar.removeAttribute('value');status.textContent='Uploading… Do not remove power.';return;}
      const percent=Math.min(100,Math.floor(event.loaded*100/event.total));bar.value=percent;
      status.textContent=percent===100?'100% uploaded. Waiting for device confirmation… Do not remove power.':'Uploading: '+percent+'%. Do not remove power.';
    });
    xhr.addEventListener('load',()=>{
      uploadActive=false;
      // ESPHome 2026.8.2 sends HTTP 200 for BOTH success and failure.
      const result=xhr.responseText.trim();
      if(xhr.status===200&&result==='Update Successful!'){
        bar.value=100;status.textContent='Update accepted. Device restarting… Reopen its page and verify the firmware version.';
      }else if(result==='Update Failed!'||xhr.status>=400){
        status.textContent='Update failed (HTTP '+xhr.status+'). '+result+' Controls remain disabled; check the device before reloading to retry.';
      }else{unconfirmed();}
    });
    for(const event of ['error','timeout','abort'])xhr.addEventListener(event,unconfirmed);
    xhr.open('POST','/update');xhr.timeout=180000;xhr.send(body);
  }catch(e){unconfirmed();}
});
</script></html>)HTML";
}  // namespace esphome::opengarage
