// Offline dashboard contract test. The only server binds to loopback and uses
// synthetic data; no ESP, Home Assistant instance or credentials are contacted.
const fs = require('fs');
const http = require('http');
const path = require('path');
const { chromium } = require(process.env.DSC_PLAYWRIGHT);

const root = path.resolve(__dirname, '../..');
const assetPaths = {'/': ['index.html', 'text/html'], '/assets/app.css': ['app.css', 'text/css'],
  '/assets/app.js': ['app.js', 'application/javascript']};
let fixture = {
  simulated: true, connected: true, bus_activity_ms: 100, trouble: null,
  partitions: [{known: true, state: 'disarmed', ready: true, fire: null, keypad_status: 1}],
  zones: [{number: 7, name: 'Zona 7 Por identificar', group: 'Por identificar', category: 'sensor', open: false},
    {number: 9, name: 'Puerta simulada', group: 'Departamento principal', category: 'door', open: true}]
};
let lastRequest = null;
let failState = false;
let promptMode = false;
let armRequests = 0;
const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  if (assetPaths[url.pathname]) {
    const [filename, type] = assetPaths[url.pathname];
    res.writeHead(200, {'Content-Type': type});
    return res.end(fs.readFileSync(path.join(root, 'web', filename)));
  }
  if (url.pathname === '/api/state') {
    if (failState) { res.writeHead(503); return res.end(); }
    res.writeHead(200, {'Content-Type':'application/json'}); return res.end(JSON.stringify(fixture));
  }
  if (url.pathname === '/api/activity') {
    res.writeHead(200, {'Content-Type':'application/json'});
    return res.end(JSON.stringify({events:[{kind:'zone_open',number:9,age_seconds:75},{kind:'disarmed',number:9,age_seconds:120},{kind:'disconnected',number:0,age_seconds:180}]}));
  }
  if (url.pathname === '/api/operation') {
    res.writeHead(200, {'Content-Type':'application/json'}); return res.end(JSON.stringify({id:42,status:promptMode?'awaiting_code':lastRequest?.body.get('kind')==='keypad'?'transmitted':'succeeded',reason:promptMode?'code_required':'none'}));
  }
  if (url.pathname === '/api/operation/code' && req.method === 'POST') {
    let body=''; req.on('data',chunk=>body+=chunk); req.on('end',()=>{
      lastRequest={url:req.url,body:new URLSearchParams(body)}; promptMode=false;
      res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({status:'accepted'}));
    }); return;
  }
  if (url.pathname === '/api/command' && req.method === 'POST') {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      lastRequest = {url:req.url, marker:req.headers['x-dsc-request'], body:new URLSearchParams(body)};
      if(lastRequest.body.get('kind')?.startsWith('arm_'))armRequests++;
      res.writeHead(200, {'Content-Type':'application/json'}); res.end(JSON.stringify({id:42,status:'queued'}));
    });
    return;
  }
  res.writeHead(404); res.end();
});

(async () => {
  await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
  const port = server.address().port;
  const browser = await chromium.launch({headless:true,executablePath:process.env.DSC_CHROME});
  try {
    for (const [width,height] of [[1440,900],[390,844]]) {
      const page = await browser.newPage({viewport:{width,height}});
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => {
        const url = new URL(route.request().url());
        return url.hostname === '127.0.0.1' && Number(url.port) === port ? route.continue() : route.abort();
      });
      await page.goto(`http://127.0.0.1:${port}/`,{waitUntil:'domcontentloaded'});
      if (await page.locator('header h1').textContent() !== 'Alarma' ||
          await page.locator('header small').textContent() !== 'Panel local' ||
          await page.locator('header #open-keypad').count() !== 1)
        throw Error('Dashboard header structure is incorrect');
      await page.getByText('Puerta simulada',{exact:true}).waitFor();
      await page.getByText('Puerta simulada: Abierta').waitFor();
      await page.getByText('Alarma desactivada',{exact:true}).waitFor();
      if (await page.locator('#alarm-detail').textContent() !== 'Lista para activar') throw Error('Ready state not shown');
      if (await page.locator('html').getAttribute('data-theme') !== 'dark') throw Error('Dark theme not default');
      await page.locator('#theme-toggle').click();
      if (await page.locator('html').getAttribute('data-theme') !== 'light') throw Error('Theme toggle failed');
      await page.locator('#theme-toggle').click();
      if (await page.locator('#trouble-health').textContent() !== 'Sin datos' ||
          await page.locator('#fire-health').textContent() !== 'Sin datos') throw Error('Unknown indicators not displayed');
      if (!await page.locator('.zone-row').filter({hasText:'Zona 7'}).getByText('En reposo').isVisible())
        throw Error('Unknown sensor category mislabeled');
      if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth)) throw Error('Horizontal overflow');
      if (await page.locator('#connection').textContent() !== 'Modo prueba' || !await page.locator('#connection-dot').evaluate(e=>e.classList.contains('test-mode')))
        throw Error('Simulation status is not shown with test indicator');
      if (await page.locator('[data-command][data-active="true"]').count()!==1 ||
          await page.locator('[data-command="disarm"]').getAttribute('data-active')!=='true' ||
          await page.locator('[data-command="disarm"]').textContent()!=='Desactivado' ||
          !await page.locator('[data-command="disarm"]').isDisabled()) throw Error('Disarmed state is not highlighted');
      for (const [state,command] of [['armed_away','arm_away'],['armed_home','arm_home'],['armed_night','arm_night']]) {
        fixture={...fixture,partitions:[{known:true,state,ready:false,fire:null,keypad_status:4}]};
        await page.waitForFunction(command=>document.querySelector(`[data-command="${command}"]`).dataset.active==='true',command,{timeout:6000});
        if (await page.locator('[data-command][data-active="true"]').count()!==1 ||
            !await page.locator(`[data-command="${command}"]`).isDisabled() ||
            await page.locator('[data-command="disarm"]').isDisabled() ||
            await page.locator('[data-command="disarm"]').textContent()!=='Desactivar') throw Error('Confirmed mode controls incorrect');
      }
      fixture={...fixture,partitions:[{known:false,state:'armed_away',ready:false,fire:null}]};
      await page.getByText('Sin información',{exact:true}).first().waitFor({timeout:6000});
      if (await page.locator('[data-command][data-active="true"]').count() ||
          !await page.locator('[data-command="disarm"]').isDisabled()) throw Error('Unknown mode was shown as confirmed');
      fixture={...fixture,partitions:[{known:true,state:'disarmed',ready:true,fire:null,keypad_status:1}]};
      await page.getByText('Lista para activar',{exact:true}).waitFor({timeout:6000});
      fixture={...fixture,simulated:false,bus_activity_ms:200};
      await page.getByText('Panel conectado',{exact:true}).waitFor({timeout:6000});
      if (!await page.locator('#connection-dot').evaluate(e=>e.classList.contains('ripple'))) throw Error('Missing bus activity ripple');
      await page.waitForTimeout(2500);
      if (await page.locator('#connection-dot').evaluate(e=>e.classList.contains('ripple'))) throw Error('Repeated stale bus activity pulsed');
      fixture={...fixture,simulated:true};
      await page.getByText('Modo prueba',{exact:true}).waitFor({timeout:6000});
      await page.locator('#open-keypad').click();
      if (await page.locator('#keypad-status').textContent() !== 'Sistema listo') throw Error('Keypad status not shown');
      await page.locator('[data-key="1"]').click();
      await page.getByText('Tecla enviada; espera la respuesta del panel.').waitFor();
      if (lastRequest?.body.get('kind') !== 'keypad' || lastRequest.body.get('key') !== '1') throw Error('Keypad request contract failed');
      await page.locator('#close-keypad').click();
      for(const command of ['arm_away','arm_home','arm_night']) {
        promptMode=true; const before=armRequests;
        await page.locator(`[data-command="${command}"]`).click();
        await page.getByText('El panel solicita tu código para activar.').waitFor({timeout:6000});
        if(lastRequest.body.get('kind')!==command || lastRequest.body.get('code')!=='')throw Error('Arm prompt request incorrect');
        await page.locator('#alarm-code').fill('2468');
        await page.locator('#command-form button[type="submit"]').click();
        for(let i=0;i<30 && lastRequest.url!=='/api/operation/code';i++)await page.waitForTimeout(100);
        if(lastRequest.body.get('id')!=='42' || lastRequest.body.get('action')!=='provide' || lastRequest.body.get('code')!=='2468' || armRequests!==before+1)throw Error('Arm code continuation incorrect');
        await page.waitForTimeout(2200);
      }
      await page.locator('[data-command="arm_away"]').click();
      await page.getByText('Confirmado por el panel.').waitFor();
      if (!lastRequest || lastRequest.url !== '/api/command' || lastRequest.marker !== '1' ||
          lastRequest.body.get('kind') !== 'arm_away' || lastRequest.body.get('code') !== '' ||
          await page.locator('#command-dialog').evaluate(e=>e.open))
        throw Error('Command request contract failed');
      fixture={...fixture,partitions:[{known:true,state:'arming',ready:false,fire:null,keypad_status:8}]};
      const cancelButton=page.locator('[data-command="disarm"]');
      await cancelButton.getByText('Cancelar',{exact:true}).waitFor({timeout:6000});
      if (await cancelButton.isDisabled()) throw Error('Cancellation disabled during exit delay');
      if (await page.locator('[data-command="arm_away"]').getAttribute('data-pending') !== 'true' ||
          await page.locator('[data-command][data-active="true"]').count() ||
          await page.locator('.mode-state').count()) throw Error('Requested mode is not distinguished during exit delay');
      await cancelButton.click();
      if (await page.locator('#dialog-title').textContent() !== 'Cancelar armado') throw Error('Cancellation dialog incorrect');
      await page.locator('#alarm-code').fill('2468');
      await page.locator('#remember-code').check();
      await page.locator('#command-form button[type="submit"]').click();
      await page.locator('#forget-code').waitFor();
      for(let i=0;i<30 && lastRequest.body.get('kind')!=='disarm';i++)await page.waitForTimeout(100);
      if (lastRequest.body.get('kind') !== 'disarm' || lastRequest.body.get('code') !== '2468')
        throw Error('Cancellation did not submit a disarm with code');
      fixture={...fixture,partitions:[{known:true,state:'disarmed',ready:true,fire:null,keypad_status:1}]};
      await page.getByText('Lista para activar',{exact:true}).waitFor({timeout:6000});
      for(const command of ['arm_home','arm_night']) {
        await page.locator(`[data-command="${command}"]`).click();
        for(let i=0;i<30 && lastRequest.body.get('kind')!==command;i++)await page.waitForTimeout(100);
        if(lastRequest.body.get('kind')!==command || lastRequest.body.get('code')!=='2468')throw Error('Remembered code missing from '+command);
        await page.waitForTimeout(300);
      }
      await page.locator('[data-command="arm_away"]').click();
      for(let i=0;i<30 && lastRequest.body.get('kind')!=='arm_away';i++)await page.waitForTimeout(100);
      if(lastRequest.body.get('code')!=='2468')throw Error('Remembered code not sent with arm request');
      fixture={...fixture,partitions:[{known:true,state:'arming',ready:false,fire:null,keypad_status:8}]};
      await cancelButton.getByText('Cancelar',{exact:true}).waitFor({timeout:6000});
      await cancelButton.click();
      for(let i=0;i<30 && lastRequest.body.get('kind')!=='disarm';i++)await page.waitForTimeout(100);
      if(lastRequest.body.get('kind')!=='disarm' || lastRequest.body.get('code')!=='2468' ||
         await page.locator('#command-dialog').evaluate(e=>e.open))throw Error('Remembered code did not submit directly');
      if(await page.evaluate(()=>JSON.stringify(localStorage).includes('2468') || JSON.stringify(sessionStorage).includes('2468')))throw Error('Code persisted in browser storage');
      await page.reload();
      await cancelButton.getByText('Cancelar',{exact:true}).waitFor({timeout:6000});
      if(await page.locator('#forget-code').isVisible())throw Error('Code survived page reload');
      await cancelButton.click();
      if(!await page.locator('#command-dialog').evaluate(e=>e.open))throw Error('Reload did not require the code again');
      await page.locator('#alarm-code').fill('2468');
      await page.locator('#remember-code').check();
      await page.locator('#command-form button[type="submit"]').click();
      await page.locator('#forget-code').waitFor();
      await page.waitForTimeout(500);
      await page.locator('#forget-code').click();
      await cancelButton.click();
      if(!await page.locator('#command-dialog').evaluate(e=>e.open))throw Error('Forgotten code did not prompt');
      await page.locator('#cancel-command').click();
      failState = true;
      await page.getByText('Sin conexión con la ESP',{exact:true}).waitFor({timeout:6000});
      if (await page.locator('[data-command][data-pending="true"]').count()) throw Error('Disconnected mode remained highlighted');
      if (await page.locator('#fire-health').textContent() !== 'Sin datos' ||
          await page.locator('#trouble-health').textContent() !== 'Sin datos' ||
          !await page.locator('.zone-row').filter({hasText:'Puerta simulada'}).getByText('Sin datos').isVisible())
        throw Error('HTTP loss retained apparently current indicators or zone state');
      failState = false;
      await page.getByText('Modo prueba',{exact:true}).waitFor({timeout:6000});
      const connectedZones = fixture.zones;
      fixture = {...fixture,connected:false,partitions:[{known:false,state:'unavailable',fire:null}],
        zones:connectedZones.map(zone => ({...zone,open:null}))};
      await page.getByText('Prueba sin conexión',{exact:true}).waitFor({timeout:6000});
      if (!await page.locator('[data-command="arm_away"]').isDisabled()) throw Error('Disconnected command enabled');
      if (!await page.locator('.zone-row').filter({hasText:'Puerta simulada'}).getByText('Sin datos').isVisible())
        throw Error('Unknown zone shown as a known state');
      if (errors.length) throw Error(errors.join('; '));
      await page.close();
      fixture = {...fixture,connected:true,partitions:[{known:true,state:'disarmed',ready:true,fire:null,keypad_status:1}],zones:connectedZones};
      lastRequest = null;
      console.log(`PASS offline dashboard ${width}px: indicators, zones, command and disconnect`);
    }
  } finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
})().catch(error => { console.error(error.message); process.exitCode=1; server.close(); });
