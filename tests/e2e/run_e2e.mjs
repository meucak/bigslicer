import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const requireFromE2e = createRequire('/e2e/package.json');
const { chromium } = requireFromE2e('playwright-core');
const axe = requireFromE2e('axe-core');

const BASE_URL = process.env.TEST_URL || 'http://127.0.0.1:8098';
const OUT_DIR = process.env.SHOT_DIR || '/out';
fs.mkdirSync(OUT_DIR, { recursive: true });

async function runAxe(page, label) {
  await page.addScriptTag({ content: axe.source });
  const results = await page.evaluate(async () => {
    return await window.axe.run(document, {
      runOnly: { type: 'tag', values: ['wcag2a', 'wcag2aa'] },
    });
  });
  if (results.violations.length > 0) {
    console.error(`[AXE FAIL ${label}]`, JSON.stringify(results.violations.map((v) => ({
      id: v.id,
      impact: v.impact,
      help: v.help,
      nodes: v.nodes.map((n) => n.html.slice(0, 120)),
    })), null, 2));
  }
  assert.equal(results.violations.length, 0, `Expected 0 axe WCAG AA violations in ${label}`);
  console.log(`  [PASS] axe-core WCAG AA (${label}): 0 violations`);
}

async function run() {
  const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium-browser',
    headless: true,
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-dev-shm-usage',
      '--use-gl=angle',
      '--use-angle=gles-egl',
      '--enable-webgl',
      '--ignore-gpu-blocklist',
      '--disable-gpu-sandbox',
    ],
  });

  const consoleErrors = [];
  const pageErrors = [];

  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    const page = await context.newPage();
    page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(m.text()); });
    page.on('pageerror', (e) => pageErrors.push(String(e)));

    console.log(`[E2E] Opening ${BASE_URL}`);
    await page.goto(BASE_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#gl-canvas');
    await page.waitForTimeout(800);

    // 1. Verify True WebGL2 backend + Native C++20 detected & demo model rendered
    const backend = await page.evaluate(() => window.__katmanBackend);
    const engText = await page.textContent('#engine-badge');
    const modelNameText = await page.textContent('#info-name');
    console.log(`  [INFO] Renderer backend: ${backend} | Engine badge: ${engText} | Model: ${modelNameText}`);
    assert.equal(backend, 'WebGL2', `Expected true WebGL2 instanced renderer, got: ${backend}`);
    assert.ok(engText.includes('Native C++20'), `Expected Native C++20 badge, got: ${engText}`);
    assert.equal(modelNameText.trim(), 'flange-housing.stl');
    await page.screenshot({ path: path.join(OUT_DIR, '01_prepare_dark.png') });

    // 2. WCAG AA axe-core check on Dark theme
    await runAxe(page, 'Prepare Dark');

    // 3. Switch to Light theme and run WCAG AA check
    await page.click('#btn-theme');
    await page.waitForTimeout(300);
    await runAxe(page, 'Prepare Light');
    await page.click('#btn-theme'); // back to dark
    await page.waitForTimeout(200);

    // 4. Slice with Gyroid infill using Native C++20 engine
    console.log('[E2E] Slicing demo flange with Gyroid infill...');
    await page.click('#btn-slice');
    await page.waitForFunction(() => !document.getElementById('results-card').classList.contains('hidden'), { timeout: 20000 });
    await page.waitForTimeout(600);

    const resTime = await page.textContent('#res-time');
    const resFil = await page.textContent('#res-filament');
    const resSpeed = await page.textContent('#res-engine-ms');
    console.log(`  [PASS] Sliced! time=${resTime}, fil=${resFil}, speed=${resSpeed}`);
    assert.ok(resTime && resTime !== '—');
    assert.ok(resFil && resFil.includes('g'));

    // Verify automatically entered Preview mode
    const previewSelected = await page.getAttribute('#btn-mode-preview', 'aria-selected');
    assert.equal(previewSelected, 'true', 'Should automatically switch to Preview mode after slicing');
    await page.screenshot({ path: path.join(OUT_DIR, '02_preview_gyroid.png') });

    // 5. Scrub layer slider down to ~45% to expose internal Gyroid infill + enable Travels
    const maxLy = Number(await page.getAttribute('#slider-layer', 'max'));
    assert.ok(maxLy > 10, `Expected >10 layers, got ${maxLy}`);
    const midLy = Math.floor(maxLy * 0.45);
    await page.evaluate((v) => {
      const s = document.getElementById('slider-layer');
      s.value = String(v);
      s.dispatchEvent(new Event('input', { bubbles: true }));
    }, midLy);
    await page.click('#btn-travels');
    await page.waitForTimeout(500);
    await page.screenshot({ path: path.join(OUT_DIR, '03_preview_midlayer_gyroid.png') });

    // 6. Switch Color Mode to Speed Heatmap & run axe-core on Preview mode
    await page.selectOption('#sel-color-mode', '1');
    await page.waitForTimeout(400);
    await runAxe(page, 'Preview Mode');
    await page.screenshot({ path: path.join(OUT_DIR, '04_preview_speed_heatmap.png') });

    // 7. Open G-Code Inspector modal and search for ;LAYER:0
    await page.click('#btn-inspect-gcode');
    await page.waitForFunction(() => !document.getElementById('gcode-modal').classList.contains('hidden'), { timeout: 10000 });
    const gcodeTxt = await page.textContent('#gcode-viewer');
    assert.ok(gcodeTxt.includes('; generated by BigSlicer'), 'G-code Inspector shows real generated G-code');
    await page.fill('#gcode-search', ';LAYER:5');
    await page.waitForTimeout(200);
    const filteredTxt = await page.textContent('#gcode-viewer');
    assert.ok(filteredTxt.includes(';LAYER:5'), 'G-code search filters cleanly');
    await page.click('#btn-gcode-close');

    // 7.5 Test Organic Tree Supports slicing in Web Studio
    await page.click('.dock-tab[data-tab="infill"]');
    await page.selectOption('#cfg-support-type', 'tree');
    await page.click('#btn-slice');
    await page.waitForFunction(
      () => !document.getElementById('btn-slice').disabled,
      { timeout: 20000 }
    );
    await page.waitForTimeout(400);
    const treeSpeed = await page.textContent('#res-engine-ms');
    console.log(`  [PASS] Organic Tree Supports sliced cleanly! (${treeSpeed})`);

    // 8. Test Offline WASM Fallback Mode (intercept /api/health -> 404 so browser runs katman.wasm!)
    console.log('[E2E] Testing Offline WebAssembly (katman.wasm) Worker mode...');
    const wasmPage = await context.newPage();
    wasmPage.on('console', (m) => {
      if (m.type() === 'error' && !m.text().includes('404 (Not Found)')) {
        consoleErrors.push(`[wasm] ${m.text()}`);
      }
    });
    wasmPage.on('pageerror', (e) => pageErrors.push(`[wasm] ${String(e)}`));
    await wasmPage.route('**/api/**', (route) => route.fulfill({ status: 404, body: 'Offline' }));
    await wasmPage.goto(BASE_URL, { waitUntil: 'networkidle' });
    await wasmPage.waitForTimeout(600);
    const wasmBadge = await wasmPage.textContent('#engine-badge');
    console.log(`  [INFO] Offline badge: ${wasmBadge}`);
    assert.ok(wasmBadge.includes('WASM'), `Expected WASM badge offline, got: ${wasmBadge}`);

    await wasmPage.click('#btn-slice');
    await wasmPage.waitForFunction(() => !document.getElementById('results-card').classList.contains('hidden'), { timeout: 30000 });
    const wasmSpeed = await wasmPage.textContent('#res-engine-ms');
    console.log(`  [PASS] Offline WASM C++20 sliced cleanly! (${wasmSpeed})`);
    assert.ok(wasmSpeed.includes('WASM'), `Expected WASM engine label, got: ${wasmSpeed}`);

    assert.equal(pageErrors.length, 0, `Uncaught page errors: ${pageErrors.join('; ')}`);
    assert.equal(consoleErrors.length, 0, `Console errors: ${consoleErrors.join('; ')}`);
    console.log('=== ALL CHROMIUM E2E + WEBGL2 + AXE-CORE + WASM TESTS PASSED ===');
  } finally {
    await browser.close();
  }
}

run().catch((err) => {
  console.error(err);
  process.exit(1);
});
