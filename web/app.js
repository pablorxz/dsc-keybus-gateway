"use strict";
const $ = (id) => document.getElementById(id);
const labels = {disarmed:["Desactivada","La alarma está desactivada"],armed_home:["Activada en casa","Modo en casa confirmado"],armed_away:["Activada al salir","Modo fuera confirmado"],armed_night:["Modo noche","Sin demora de entrada"],arming:["Activando","Esperando confirmación del panel"],pending:["Entrada en curso","El panel está en cuenta regresiva"],triggered:["Alarma disparada","Revisa el panel y las zonas"],unavailable:["Sin información","No hay conexión con el panel"]};
const descriptions = {motion:"Movimiento",door:"Puerta",window:"Ventana",sensor:"Sensor"};
let currentState=null, operationId=null, refreshing=false, polling=false, commandSubmitting=false;
let rememberedCode=null;
let codeOperationId=null, providingCode=false;
let requestedMode=null, sawExitDelay=false;
let lastBusActivity=null;
let keypadBusy=false, keypadUncertain=false;
let activityBusy=false, showAllActivity=false;
let storedTheme="dark";
try { storedTheme=localStorage.getItem("dsc-theme")||"dark"; } catch {}
function applyTheme(value) { document.documentElement.dataset.theme=value; $("theme-toggle").textContent=value==="dark"?"☼":"☾"; $("theme-toggle").setAttribute("aria-label",value==="dark"?"Activar tema claro":"Activar tema oscuro"); }
applyTheme(storedTheme==="light"?"light":"dark");
$("theme-toggle").addEventListener("click",()=>{ const next=document.documentElement.dataset.theme==="dark"?"light":"dark"; applyTheme(next); try { localStorage.setItem("dsc-theme",next); } catch {} });
// Panel status values informed by the taligentx reference; this is not an LCD copy.
const keypadMessages={1:"Sistema listo",2:"Zonas interiores abiertas",3:"Zonas abiertas",4:"Armada en casa",5:"Armada al salir",6:"Armada sin demora de entrada",7:"No se pudo armar",8:"Demora de salida",9:"Armando sin demora de entrada",12:"Demora de entrada",14:"Función no disponible",16:"Teclado bloqueado",17:"Alarma activada",61:"Desarmada: memoria de alarma",62:"Sistema desarmado",143:"Código incorrecto",158:"Ingrese función *",159:"Ingrese código de acceso",160:"*1: Anulación de zonas",161:"*2: Averías",162:"*3: Memoria de alarma",165:"Ingrese código maestro",166:"*5: Códigos de acceso",169:"*6: Funciones de usuario",170:"*6: Fecha y hora",183:"Ingrese código de instalador",184:"Ingrese función *"};
function renderKeypad() {
  const p=currentState?.partitions?.[0];
  const available=!!currentState?.connected && !!p?.known;
  $("keypad-status").textContent=!available?"Sin información del panel":keypadMessages[p.keypad_status] || (p.keypad_status==null?"Estado no disponible":`Estado del panel: 0x${p.keypad_status.toString(16).toUpperCase().padStart(2,"0")}`);
  document.querySelectorAll("[data-key]").forEach(button=>{button.disabled=!available || keypadBusy || keypadUncertain;});
  $("open-keypad").disabled=!available;
}

async function request(url, options={}) {
  const controller=new AbortController();
  const timeout=setTimeout(()=>controller.abort(),8000);
  try { return await fetch(url,{...options,signal:controller.signal}); }
  finally { clearTimeout(timeout); }
}

function node(tag,className,text) { const e=document.createElement(tag); if(className)e.className=className; if(text!==undefined)e.textContent=text; return e; }
function renderZones(zones) {
  const container=$("zone-groups"); container.replaceChildren();
  const groups=new Map();
  for(const zone of zones) { if(!groups.has(zone.group))groups.set(zone.group,[]); groups.get(zone.group).push(zone); }
  for(const [group,items] of groups) {
    const card=node("section","group-card"), header=node("div","group-title"), title=node("h3","",group);
    header.append(title,node("small","",`${items.length} ${items.length===1?"zona":"zonas"}`)); card.append(header);
    for(const zone of items) {
      const row=node("div","zone-row"), icon=node("span","zone-icon",zone.category==="motion"?"◉":zone.category==="window"?"▦":zone.category==="door"?"▯":"◇");
      icon.setAttribute("aria-hidden","true");
      const name=node("div","zone-name"); name.append(node("strong","",zone.name),node("small","",`Zona ${zone.number} · ${descriptions[zone.category]||"Sensor"}`));
      const state=node("span","zone-state"), dot=node("span","zone-dot"+(zone.open===null?"":zone.open?" open":" closed"));
      const text=zone.open===null?"Sin datos":zone.open?(zone.category==="motion"?"Movimiento":zone.category==="sensor"?"Activada":"Abierta"):(zone.category==="motion"||zone.category==="sensor"?"En reposo":"Cerrada");
      state.append(dot,document.createTextNode(text));
      row.append(icon,name,state); card.append(row);
    }
    container.append(card);
  }
  if(!groups.size) container.append(node("p","empty","No hay zonas configuradas."));
  $("zone-count").textContent=`${zones.length} zonas`;
}
function activityAge(seconds) {
  if(seconds<60)return "Ahora";
  if(seconds<3600)return `Hace ${Math.floor(seconds/60)} min`;
  if(seconds<86400)return `Hace ${Math.floor(seconds/3600)} h`;
  return `Hace ${Math.floor(seconds/86400)} d`;
}
async function refreshActivity() {
  if(activityBusy || !currentState)return; activityBusy=true;
  try {
    const response=await request("/api/activity",{cache:"no-store",credentials:"same-origin"});
    if(!response.ok)throw new Error("activity");
    const data=await response.json(); const container=$("activity-list"); container.replaceChildren();
    const zones=new Map((currentState?.zones||[]).map(zone=>[zone.number,zone]));
    const events=(data.events||[]).filter(event=>!event.kind.startsWith("zone_")||zones.has(event.number));
    const shown=showAllActivity?events:events.slice(0,8);
    const names={connected:"Panel conectado",disconnected:"Panel desconectado",armed:"Alarma activada",disarmed:"Alarma desactivada",trouble:"Avería indicada",trouble_cleared:"Avería restablecida",alarm:"Alarma disparada",alarm_cleared:"Alarma restablecida",fire:"Incendio indicado",fire_cleared:"Indicación de incendio restablecida"};
    for(const event of shown) {
      const zone=event.kind.startsWith("zone_")?zones.get(event.number):null;
      const description=zone?(event.kind==="zone_open"?(zone.category==="motion"?"Movimiento detectado":zone.category==="sensor"?"Activada":"Abierta"):(zone.category==="motion"?"Movimiento en reposo":zone.category==="sensor"?"En reposo":"Cerrada")):names[event.kind]||"Evento observado";
      const row=node("div","activity-row"); row.append(node("span","activity-time",activityAge(event.age_seconds)),node("span","activity-text",zone?`${zone.name}: ${description}`:description),node("span","activity-meta",zone?`Zona ${zone.number}`:event.number?`Partición ${event.number}`:"Panel")); container.append(row);
    }
    if(!shown.length)container.append(node("p","empty","Todavía no hay cambios observados."));
    if(events.length>8) { const more=node("button","activity-more",showAllActivity?"Mostrar menos":"Ver toda la actividad"); more.type="button"; more.addEventListener("click",()=>{showAllActivity=!showAllActivity;refreshActivity();}); container.append(more); }
  } catch { $("activity-list").replaceChildren(node("p","empty activity-error","No se pudo cargar la actividad.")); }
  finally { activityBusy=false; }
}
function render(data) {
  currentState=data; const partition=data.partitions[0]||{state:"unavailable"};
  const known=!!data.connected && partition.known===true && Object.prototype.hasOwnProperty.call(labels,partition.state) && partition.state!=="unavailable";
  const status=known?labels[partition.state]:labels.unavailable;
  const dot=$("connection-dot");
  $("connection").textContent=data.simulated?(data.connected?"Modo prueba":"Prueba sin conexión"):(data.connected?"Panel conectado":"Panel sin conexión");
  dot.className="status-dot "+(data.simulated?"test-mode":data.connected?"online":"offline");
  $("connection").title=data.simulated?"Datos simulados":data.connected?"Actividad de reloj Keybus detectada recientemente; no verifica todos los datos.":"No se detecta actividad reciente del Keybus.";
  if(data.connected && !data.simulated && Number.isInteger(data.bus_activity_ms)) {
    if(lastBusActivity!==data.bus_activity_ms) { void dot.offsetWidth; dot.classList.add("ripple"); }
    lastBusActivity=data.bus_activity_ms;
  } else lastBusActivity=null;
  $("alarm-state").textContent=status[0];
  $("alarm-detail").textContent=known && partition.state==="disarmed"?(partition.ready===true?"Lista para activar":partition.ready===false?"No está lista para activar":"Disponibilidad sin datos"):status[1];
  const dotState=!known?"offline":partition.state==="triggered"?"alarm-triggered":partition.state==="disarmed"?"online":["armed_home","armed_away","armed_night"].includes(partition.state)?"alarm-armed":"offline";
  $("alarm-dot").className="large-dot "+dotState;
  $("panel-health").textContent=data.connected?(data.simulated?"Simulado":"Conectado"):"Sin conexión";
  $("trouble-health").textContent=!data.connected || data.trouble===null?"Sin datos":data.trouble?"Revisar":"Sin averías";
  $("fire-health").textContent=!data.connected || partition.fire===null?"Sin datos":partition.fire?"Detectado":"Sin indicación";
  const disarmed=known && partition.state==="disarmed";
  if(!known || (sawExitDelay && partition.state!=="arming") || partition.state.startsWith("armed_") || partition.state==="triggered") {
    requestedMode=null; sawExitDelay=false;
  }
  if(known && partition.state==="arming" && requestedMode) sawExitDelay=true;
  document.querySelector('[data-command="disarm"]').textContent=disarmed?"Desactivado":known && partition.state==="arming"?"Cancelar":"Desactivar";
  const activeModes={arm_away:"armed_away",arm_home:"armed_home",arm_night:"armed_night"};
  document.querySelectorAll("[data-command]").forEach(button=>{
    const active=known && (button.dataset.command==="disarm"?disarmed:activeModes[button.dataset.command]===partition.state);
    const pending=known && partition.state==="arming" && requestedMode===button.dataset.command;
    button.disabled=commandSubmitting || !known || (button.dataset.command==="disarm"?disarmed:!disarmed);
    button.dataset.active=active?"true":"false";
    if(button.dataset.command!=="disarm") {
      button.dataset.pending=pending?"true":"false";
    }
  });
  renderZones(data.zones||[]);
  renderKeypad();
}
async function refresh() {
  if(refreshing)return; refreshing=true;
  try { const response=await request("/api/state",{cache:"no-store",credentials:"same-origin"}); if(!response.ok)throw new Error("state"); render(await response.json()); }
  catch {
    const zones=currentState?.zones;
    currentState=null;
    requestedMode=null; sawExitDelay=false;
    lastBusActivity=null;
    $("connection").textContent="Sin conexión con la ESP"; $("connection").title="La ESP no respondió a la consulta."; $("connection-dot").className="status-dot offline";
    $("alarm-state").textContent="Sin información"; $("alarm-detail").textContent="No hay conexión con la ESP";
    $("alarm-dot").className="large-dot offline"; $("panel-health").textContent="Sin datos";
    $("trouble-health").textContent="Sin datos"; $("fire-health").textContent="Sin datos";
    $("activity-list").replaceChildren(node("p","empty activity-error","Sin conexión con la ESP."));
    if(zones)renderZones(zones.map(zone=>({...zone,open:null})));
    document.querySelectorAll("[data-command]").forEach(button=>{button.disabled=true;button.dataset.active="false";button.dataset.pending="false";});
    document.querySelector('[data-command="disarm"]').textContent="Desactivar";
    renderKeypad();
  }
  finally { refreshing=false; }
}
async function pollOperation() {
  if(!operationId || polling)return; polling=true;
  const polledId=operationId;
  try {
    const response=await request(`/api/operation?id=${encodeURIComponent(polledId)}`,{cache:"no-store"});
    if(!response.ok)throw new Error("operation");
    const operation=await response.json();
    if(operationId!==polledId)return;
    const text={queued:"Acción en espera…",sending:"Enviando al panel…",awaiting_panel:"Esperando confirmación…",succeeded:"Confirmado por el panel.",transmitted:"Teclas enviadas.",rejected:"El panel rechazó la acción.",timed_out:"No llegó confirmación a tiempo.",uncertain:"El resultado no se pudo confirmar."};
    if(operation.status==="awaiting_code") {
      $("operation").textContent="El panel solicita el código para activar.";
      if(!providingCode && codeOperationId!==polledId && !$('command-dialog').open) {
        codeOperationId=polledId; $("dialog-title").textContent="Activar alarma";
        $("dialog-copy").textContent="El panel solicita tu código para activar.";
        $("alarm-code").value=""; $("remember-code").checked=false; $("command-dialog").showModal(); $("alarm-code").focus();
      }
      return;
    }
    if(codeOperationId===polledId) { codeOperationId=null; if($("command-dialog").open)closeCommandDialog(); }
    $("operation").textContent=operation.reason==="code_required"?"No se completó la solicitud de código. Revisa el teclado físico.":text[operation.status]||"Resultado no disponible.";
    if(["rejected","timed_out","uncertain","unknown"].includes(operation.status)) { requestedMode=null; sawExitDelay=false; if(currentState)render(currentState); }
    if(["succeeded","transmitted","rejected","timed_out","uncertain","unknown"].includes(operation.status))operationId=null;
  } catch { if(operationId===polledId) $("operation").textContent="No se pudo consultar la acción."; }
  finally { polling=false; }
}
async function submitCommand(submittedCommand,code="") {
  if(commandSubmitting)return;
  commandSubmitting=true; if(currentState)render(currentState);
  $("operation").textContent="Enviando solicitud…";
  try {
    const response=await request("/api/command",{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/x-www-form-urlencoded","X-DSC-Request":"1"},body:new URLSearchParams({kind:submittedCommand,partition:"1",code})});
    const data=await response.json(); if(!response.ok)throw new Error(data.reason||"rechazada");
    if(submittedCommand.startsWith("arm_")) { requestedMode=submittedCommand; sawExitDelay=false; if(currentState)render(currentState); }
    operationId=data.id; await pollOperation();
  } catch(error) { $("operation").textContent="No se pudo iniciar la acción: "+error.message; }
  finally { commandSubmitting=false; if(currentState)render(currentState); }
}
document.querySelectorAll("[data-command]").forEach(button=>button.addEventListener("click",()=>{
  if(button.disabled || commandSubmitting)return;
  if(button.dataset.command!=="disarm") { void submitCommand(button.dataset.command,rememberedCode||""); return; }
  if(rememberedCode!==null) { void submitCommand("disarm",rememberedCode); return; }
  const cancelArming=currentState?.partitions?.[0]?.state==="arming";
  $("dialog-title").textContent=cancelArming?"Cancelar armado":"Desactivar alarma";
  $("dialog-copy").textContent=cancelArming?"Introduce tu código para cancelar el armado.":"Introduce tu código para desactivar la alarma.";
  $("alarm-code").value=""; $("remember-code").checked=false; $("command-dialog").showModal(); $("alarm-code").focus();
}));
function closeCommandDialog() { $("alarm-code").value=""; $("command-dialog").close(); }
async function abandonCode(id) {
  try { const response=await request("/api/operation/code",{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/x-www-form-urlencoded","X-DSC-Request":"1"},body:new URLSearchParams({id:String(id),action:"cancel"})}); if(!response.ok)throw Error(); }
  catch { $("operation").textContent="No se pudo cancelar la solicitud. Revisa el teclado físico."; }
}
$("cancel-command").addEventListener("click",closeCommandDialog);
$("close-command").addEventListener("click",closeCommandDialog);
$("command-dialog").addEventListener("close",()=>{ $("alarm-code").value=""; if(codeOperationId!==null) { const id=codeOperationId; codeOperationId=null; if(operationId===id)operationId=null; $("operation").textContent="Solicitud abandonada. Revisa el teclado físico."; void abandonCode(id); } });
$("forget-code").addEventListener("click",()=>{ rememberedCode=null; $("forget-code").hidden=true; });
$("command-form").addEventListener("submit",async event=>{
  event.preventDefault(); const code=$("alarm-code").value;
  if(!/^\d{4}(\d{2})?$/.test(code)) { $("alarm-code").setCustomValidity("Usa un código de 4 o 6 dígitos."); $("alarm-code").reportValidity(); $("alarm-code").setCustomValidity(""); return; }
  if($("remember-code").checked) { rememberedCode=code; $("forget-code").hidden=false; }
  if(codeOperationId!==null) {
    const id=codeOperationId; codeOperationId=null; providingCode=true; closeCommandDialog();
    try { const response=await request("/api/operation/code",{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/x-www-form-urlencoded","X-DSC-Request":"1"},body:new URLSearchParams({id:String(id),action:"provide",code})});
      if(!response.ok)throw Error(); $("operation").textContent="Enviando código al panel…";
    } catch { $("operation").textContent="No se pudo enviar el código. Revisa el teclado físico."; operationId=null; }
    finally { providingCode=false; }
  } else { closeCommandDialog(); void submitCommand("disarm",code); }
});
$("open-keypad").addEventListener("click",()=>{ $("keypad-dialog").showModal(); renderKeypad(); });
$("close-keypad").addEventListener("click",()=>$("keypad-dialog").close());
document.querySelectorAll("[data-key]").forEach(button=>button.addEventListener("click",async()=>{
  if(keypadBusy || keypadUncertain || !currentState?.connected)return;
  keypadBusy=true; $("keypad-result").textContent="Enviando tecla…"; renderKeypad();
  try {
    const response=await request("/api/command",{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/x-www-form-urlencoded","X-DSC-Request":"1"},body:new URLSearchParams({kind:"keypad",partition:"1",key:button.dataset.key})});
    const result=await response.json();
    if(!response.ok) { $("keypad-result").textContent="La tecla fue rechazada: "+(result.reason||"sin detalle"); return; }
    const deadline=Date.now()+12000;
    while(Date.now()<deadline) {
      await new Promise(resolve=>setTimeout(resolve,400));
      const statusResponse=await request(`/api/operation?id=${encodeURIComponent(result.id)}`,{cache:"no-store"});
      if(!statusResponse.ok)throw new Error("operation");
      const status=(await statusResponse.json()).status;
      if(status==="transmitted") { $("keypad-result").textContent="Tecla enviada; espera la respuesta del panel."; return; }
      if(["rejected","timed_out","uncertain","unknown"].includes(status)) { keypadUncertain=true; $("keypad-result").textContent="No se pudo confirmar el resultado. Revisa el teclado físico antes de continuar."; return; }
    }
    throw new Error("timeout");
  } catch { keypadUncertain=true; $("keypad-result").textContent="Resultado incierto. Revisa el teclado físico antes de continuar."; }
  finally { keypadBusy=false; renderKeypad(); }
}));
refresh().then(refreshActivity); setInterval(()=>{ refresh(); pollOperation(); },2000); setInterval(refreshActivity,5000);
