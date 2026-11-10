// Invoked only on the authorized simulated bench. Credentials arrive on stdin.
const fs = require('fs');
const { chromium } = require(process.env.DSC_PLAYWRIGHT);
(async () => {
  const config = JSON.parse(fs.readFileSync(0, 'utf8'));
  const browser = await chromium.launch({headless: true, executablePath: config.chrome});
  try {
    for (const [name, width, height] of [['desktop', 1440, 1000], ['mobile', 390, 844]]) {
      const context = await browser.newContext({viewport: {width, height}, httpCredentials: config.auth});
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      const response = await page.goto(config.url, {waitUntil: 'networkidle', timeout: 30000});
      if (!response.ok()) throw new Error(`HTTP ${response.status()}`);
      await page.locator('#zone-groups .empty').waitFor({state:'hidden', timeout:15000});
      if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth))
        throw new Error(`${name}: horizontal overflow`);
      if (!await page.locator('#simulation').isVisible()) throw new Error('Simulation banner missing');
      if (!await page.locator('.zone-row').filter({hasText:'Zona 7'}).getByText('En reposo').isVisible())
        throw new Error('Unclassified sensor uses an incorrect state label');
      await page.screenshot({path: `${config.output}/${name}.png`, fullPage: true});
      await page.locator('[data-command="arm_away"]').click();
      if (!await page.locator('#command-dialog').isVisible()) throw new Error('Command dialog did not open');
      await page.locator('#cancel-command').click();
      if (errors.length) throw new Error(errors.join('; '));
      console.log(`PASS ${name}: layout, simulation label, dialog, no script errors`);
      await context.close();
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error.message); process.exit(1); });
