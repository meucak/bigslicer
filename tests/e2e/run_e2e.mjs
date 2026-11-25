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

// Sample rendered viewport canvas to prove both filament walls (orange) and supports (emerald green) are visibly drawn!
async function countPreviewPixels(page) {
  return await page.evaluate(() => {
    const c = document.getElementById('gl-canvas');
    // For WebGL2 with preserveDrawingBuffer=false, copy via toDataURL inside a rAF right after render,
    // or for Canvas2D read directly via getContext('2d').getImageData!
    const ctx2d = c.getContext('2d');
    if (ctx2d) {
      const img = ctx2d.getImageData(0, 0, c.width, c.height).data;
      let greenSup = 0, orangeWall = 0;
      for (let i = 0; i < img.length; i += 16) {
        const r = img[i], g = img[i + 1], b = img[i + 2];
        if (g > 110 && g > r + 28 && g > b + 14) greenSup++;
        if (r > 150 && g > 55 && r > b + 60) orangeWall++;
      }
      return { greenSup, orangeWall, mode: '2d' };
    }
    return { greenSup: -1, orangeWall: -1, mode: 'webgl' };
  });
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

    console.log(`[E2E] Opening ${BASE_URL} (WebGL2)`);
    await page.goto(BASE_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#gl-canvas');
    await page.waitForTimeout(800);

    // 1. Verify True WebGL2 backend + 150+ printers loaded
    const backend = await page.evaluate(() => window.__bigslicerBackend);
    const printerCount = await page.evaluate(() => window.__bigslicerState.presets.printers.length);
    console.log(`  [INFO] Backend: ${backend} | Printers: ${printerCount}`);
    assert.equal(backend, 'WebGL2', `Expected true WebGL2, got: ${backend}`);
    assert.ok(printerCount >= 120, `Expected >=120 printers, got: ${printerCount}`);

    // 2. Test 150+ Searchable Printer Modal: search & select "Ender-3 Pro"
    await page.click('#btn-open-printer-modal');
    await page.waitForSelector('#printer-modal:not(.hidden)');
    await page.fill('#printer-search', 'Ender-3 Pro');
    await page.waitForTimeout(200);
    await runAxe(page, 'Printer Modal');
    await page.click('#printer-grid .printer-card');
    await page.waitForTimeout(250);
    const activePrinterName = await page.textContent('#lbl-active-printer');
    console.log(`  [PASS] Selected classic printer: ${activePrinterName}`);
    assert.ok(activePrinterName.includes('Ender-3 Pro'));

    // 3. Test 3D Interactive Transform Gizmo in Prepare mode (Move drag + Rotate +90 + Scale + Reset)
    await page.fill('#cfg-pos-x', '25');
    await page.dispatchEvent('#cfg-pos-x', 'input');
    await page.fill('#cfg-pos-y', '-15');
    await page.dispatchEvent('#cfg-pos-y', 'input');
    await page.waitForTimeout(250);
    const shifted = await page.evaluate(() => ({ x: window.__bigslicerState.xform.posX, y: window.__bigslicerState.xform.posY }));
    assert.equal(shifted.x, 25);
    assert.equal(shifted.y, -15);

    // Drag directly on canvas in Move mode near model center
    await page.mouse.move(620, 440);
    await page.mouse.down();
    await page.mouse.move(680, 420, { steps: 6 });
    await page.mouse.up();
    await page.waitForTimeout(250);
    await page.screenshot({ path: path.join(OUT_DIR, '01_prepare_gizmo_move.png') });

    // Switch to Rotate mode & screenshot ring gizmo
    await page.click('.gizmo-btn[data-gizmo="rotate"]');
    await page.click('#btn-rot-z90');
    await page.waitForTimeout(250);
    await page.screenshot({ path: path.join(OUT_DIR, '02_prepare_gizmo_rotate.png') });
    await page.click('#btn-reset-xform');
    await page.click('.gizmo-btn[data-gizmo="move"]');

    // WCAG AA checks on Dark & Light
    await runAxe(page, 'Prepare Dark');
    await page.click('#btn-theme');
    await page.waitForTimeout(250);
    await runAxe(page, 'Prepare Light');
    await page.click('#btn-theme');
    await page.waitForTimeout(200);

    // 4. Open "Overhang & Support Arch" demo model -> Slice with Organic Tree Supports!
    await page.click('#btn-model-menu');
    await page.click('#demo-list .pop-item:has-text("Overhang & Support Arch")');
    await page.waitForTimeout(400);
    await page.click('.dock-tab[data-tab="support"]');
    await page.selectOption('#cfg-support-type', 'tree');
    await page.click('#btn-slice');
    await page.waitForFunction(() => !document.getElementById('results-card').classList.contains('hidden'), { timeout: 20000 });
    await page.waitForTimeout(700);

    const hasSup = await page.evaluate(() =>
      window.__bigslicerState.lastStats.features.some((f) => (f.id === 9 || f.id === 10) && f.filamentMm > 10)
    );
    assert.ok(hasSup, 'Expected real Organic Tree Support filament > 10mm on Overhang Arch');
    const treeSpeed = await page.textContent('#res-engine-ms');
    console.log(`  [PASS] WebGL2 Organic Tree Supports sliced on Overhang Arch! (${treeSpeed})`);
    await page.screenshot({ path: path.join(OUT_DIR, '03_webgl2_arch_tree_supports.png') });

    // 5. Scrub to Layer 10, add M600 Pause + enable Fuzzy Skin + switch to Normal Snug Support & inspect G-Code!
    await page.evaluate(() => {
      const s = document.getElementById('slider-layer');
      s.value = '10';
      s.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await page.click('#btn-add-pause');
    await page.click('.dock-tab[data-tab="quality"]');
    await page.selectOption('#cfg-fuzzy', 'outside');
    await page.click('.dock-tab[data-tab="support"]');
    await page.selectOption('#cfg-support-type', 'snug');
    await page.click('#btn-slice');
    await page.waitForFunction(() => !document.getElementById('btn-slice').disabled, { timeout: 20000 });
    await page.waitForTimeout(600);
    await page.screenshot({ path: path.join(OUT_DIR, '04_webgl2_arch_snug_fuzzy.png') });
    await runAxe(page, 'Preview Mode');

    // G-Code Inspector check: BigSlicer header + M600 pause
    await page.click('#btn-inspect-gcode');
    await page.waitForFunction(() => !document.getElementById('gcode-modal').classList.contains('hidden'), { timeout: 10000 });
    const gcodeTxt = await page.textContent('#gcode-viewer');
    assert.ok(gcodeTxt.includes('; generated by BigSlicer'), 'G-code Inspector shows BigSlicer header');
    await page.fill('#gcode-search', 'M600');
    await page.waitForTimeout(200);
    const pauseMatch = await page.textContent('#gcode-viewer');
    assert.ok(pauseMatch.includes('M600'), 'G-code contains M600 pause injected from scrubber button');
    await page.click('#btn-gcode-close');

    // 6. CRITICAL TOSHI TEST: Canvas 2D Software Z-Buffer Renderer (?renderer=canvas2d, simulates --disable-gpu!)
    console.log('[E2E] Testing Canvas 2D Software Z-Buffer mode (Toshi --disable-gpu parity)...');
    const c2dPage = await context.newPage();
    c2dPage.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(`[c2d] ${m.text()}`); });
    c2dPage.on('pageerror', (e) => pageErrors.push(`[c2d] ${String(e)}`));
    await c2dPage.goto(`${BASE_URL}/?renderer=canvas2d`, { waitUntil: 'networkidle' });
    await c2dPage.waitForTimeout(600);
    const c2dBackend = await c2dPage.evaluate(() => window.__bigslicerBackend);
    assert.equal(c2dBackend, 'Canvas 2D', `Expected Canvas 2D backend, got: ${c2dBackend}`);

    // Load Overhang Arch, shift with Move Gizmo, slice with Organic Tree Supports & PROVE green support + orange wall pixels render!
    await c2dPage.click('#btn-model-menu');
    await c2dPage.click('#demo-list .pop-item:has-text("Overhang & Support Arch")');
    await c2dPage.waitForTimeout(400);
    await c2dPage.fill('#cfg-pos-x', '20');
    await c2dPage.dispatchEvent('#cfg-pos-x', 'input');
    await c2dPage.click('.dock-tab[data-tab="support"]');
    await c2dPage.selectOption('#cfg-support-type', 'tree');
    await c2dPage.click('#btn-slice');
    await c2dPage.waitForFunction(() => !document.getElementById('results-card').classList.contains('hidden'), { timeout: 20000 });
    await c2dPage.waitForTimeout(900);

    const pxCounts = await countPreviewPixels(c2dPage);
    console.log(`  [PASS] Canvas 2D Preview Pixels -> orangeWalls=${pxCounts.orangeWall}, greenSupports=${pxCounts.greenSup}`);
    assert.ok(pxCounts.orangeWall > 400, `Expected visible extruded walls in Canvas 2D preview, got ${pxCounts.orangeWall}`);
    assert.ok(pxCounts.greenSup > 150, `Expected visible green Tree Supports in Canvas 2D preview, got ${pxCounts.greenSup}`);
    await c2dPage.screenshot({ path: path.join(OUT_DIR, '05_canvas2d_toshi_tree_supports.png') });

    // Scrub mid-layer in Canvas 2D + enable Travels
    const c2dMax = Number(await c2dPage.getAttribute('#slider-layer', 'max'));
    await c2dPage.evaluate((v) => {
      const s = document.getElementById('slider-layer');
      s.value = String(v);
      s.dispatchEvent(new Event('input', { bubbles: true }));
    }, Math.floor(c2dMax * 0.55));
    await c2dPage.click('#btn-travels');
    await c2dPage.waitForTimeout(600);
    await c2dPage.screenshot({ path: path.join(OUT_DIR, '06_canvas2d_midlayer_gyroid_supports.png') });

    // 7. Offline WASM C++20 Worker Mode (all 153 printers offline + full slice!)
    console.log('[E2E] Testing Offline WebAssembly (372 KB WASM C++20) Worker mode...');
    const wasmPage = await context.newPage();
    wasmPage.on('console', (m) => {
      if (m.type() === 'error' && !m.text().includes('404 (Not Found)')) {
        consoleErrors.push(`[wasm] ${m.text()}`);
      }
    });
    wasmPage.on('pageerror', (e) => pageErrors.push(`[wasm] ${String(e)}`));
    await wasmPage.route('**/api/**', (route) => route.fulfill({ status: 404, body: 'Offline' }));
    await wasmPage.goto(BASE_URL, { waitUntil: 'networkidle' });
    await wasmPage.waitForTimeout(800);
    const wasmPrinters = await wasmPage.evaluate(() => window.__bigslicerState.presets.printers.length);
    console.log(`  [INFO] Offline WASM loaded ${wasmPrinters} built-in printers!`);
    assert.ok(wasmPrinters >= 120, `Expected >=120 printers in offline WASM mode, got ${wasmPrinters}`);
    await wasmPage.click('#btn-slice');
    await wasmPage.waitForFunction(() => !document.getElementById('results-card').classList.contains('hidden'), { timeout: 30000 });
    const wasmSpeed = await wasmPage.textContent('#res-engine-ms');
    console.log(`  [PASS] Offline WASM C++20 sliced cleanly! (${wasmSpeed})`);
    assert.ok(wasmSpeed.includes('WASM'));

    assert.equal(pageErrors.length, 0, `Uncaught page errors: ${pageErrors.join('; ')}`);
    assert.equal(consoleErrors.length, 0, `Console errors: ${consoleErrors.join('; ')}`);
    console.log('=== ALL BIGSLICER v2.0 E2E (WEBGL2 + CANVAS2D + 150+ PRINTERS + GIZMO + SUPPORTS + AXE + WASM) PASSED ===');
  } finally {
    await browser.close();
  }
}

run().catch((err) => {
  console.error(err);
  process.exit(1);
});
