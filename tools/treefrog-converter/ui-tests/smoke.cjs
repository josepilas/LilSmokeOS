"use strict";

// Runs real browser workers and the shipped WASM. Fixtures use .NET ZIP, not a JS ZIP engine.
// NODE_PATH must include Playwright. Optional TF_TEST_ZIP stays outside the served directory.
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const http = require("node:http");
const { execFileSync } = require("node:child_process");
const { chromium } = require("playwright");
const dist = path.resolve(__dirname, "../site/dist");
const artifacts = path.resolve(__dirname, "../_build/ui-tests");
const mime = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm", ".zip": "application/zip" };
const requests = [];
const pageErrors = [];

function fixture(mode, core) {
  const args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path.join(__dirname, "fixture.ps1"), "-Mode", mode];
  if (core) args.push("-CorePath", core);
  return Buffer.from(execFileSync("powershell.exe", args, { encoding: "utf8", maxBuffer: 16 * 1024 * 1024 }).trim(), "base64");
}
async function state(page, expected) {
  await page.waitForFunction((value) => [value, "error"].includes(document.getElementById("app").dataset.state), expected, { timeout: 60000 });
  const actual = await page.getByTestId("converter").getAttribute("data-state");
  assert.equal(actual, expected, `${await page.getByTestId("status-message").textContent()} ${await page.getByTestId("status-detail").textContent()}`);
}
async function overflow(page) {
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, "viewport must not overflow horizontally");
}
async function analyze(page, buffer, name = "unsupported.zip") {
  await page.getByTestId("archive-input").setInputFiles({ name, mimeType: "application/zip", buffer });
  await state(page, "selected");
  await page.getByTestId("analyze").click();
  await state(page, "analyzed");
  assert.equal(await page.getByTestId("port-select").inputValue(), "0", "single port auto-selection");
  await page.getByTestId("port-select").selectOption("0");
}
async function main() {
  await fs.mkdir(artifacts, { recursive: true });
  const server = http.createServer(async (request, response) => {
    if (request.method !== "GET" && request.method !== "HEAD") { response.writeHead(405).end(); return; }
    try {
      const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
      const target = path.resolve(dist, "." + (pathname === "/" ? "/index.html" : pathname));
      if (!target.startsWith(dist + path.sep) || pathname.includes("/.")) { response.writeHead(403).end(); return; }
      const data = await fs.readFile(target);
      response.writeHead(200, { "Content-Type": mime[path.extname(target)] || "application/octet-stream", "Cache-Control": "no-store" });
      response.end(request.method === "HEAD" ? undefined : data);
    } catch (_) { response.writeHead(404).end(); }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  let browser;
  try {
    browser = await chromium.launch({ channel: process.env.TF_BROWSER_CHANNEL || "msedge", headless: true });
    const context = await browser.newContext({ acceptDownloads: true, viewport: { width: 1280, height: 1000 } });
    context.on("request", (request) => {
      // Browser-owned download panels are not application network activity.
      if (/^(edge|chrome):/.test(request.url())) return;
      requests.push({ url: request.url(), method: request.method(), body: request.postData() });
    });
    const page = await context.newPage();
    page.on("pageerror", (error) => pageErrors.push(error.message));
    await page.goto(origin);
    await state(page, "idle");
    assert.match(await page.getByTestId("tool-objective").textContent(), /native TreeFrog compatibility cannot handle/);
    assert.match(await page.getByTestId("tool-objective").textContent(), /LILROMS\/treefrog-ports/);
    assert.match(await page.getByTestId("tool-objective").textContent(), /\/storage\/treefrog-ports/);
    assert.equal(await page.getByTestId("convert").isDisabled(), true);
    assert.equal(await page.getByTestId("download").isDisabled(), true);
    assert.equal(await page.locator('[data-reported="true"]').count(), 0);
    await overflow(page);
    await page.screenshot({ path: path.join(artifacts, "desktop-idle.png"), fullPage: true });
    for (const width of [390, 320]) {
      await page.setViewportSize({ width, height: 844 });
      await overflow(page);
      await page.screenshot({ path: path.join(artifacts, `mobile-${width}-idle.png`), fullPage: true });
    }
    await page.setViewportSize({ width: 1280, height: 1000 });
    await page.getByTestId("archive-input").setInputFiles({ name: "wrong.txt", mimeType: "text/plain", buffer: Buffer.from("not a ZIP") });
    await state(page, "error");
    assert.match(await page.getByTestId("status-message").textContent(), /\.zip/);
    assert.equal(await page.getByTestId("converter").getAttribute("data-worker"), "stopped");

    const unsupported = fixture("unsupported");
    await analyze(page, unsupported);
    await state(page, "blocked");
    assert.equal(await page.getByTestId("convert").isDisabled(), true);
    assert.match(await page.getByTestId("port-reason").textContent(), /ELF|MIPS/);
    assert.ok(await page.locator('[data-reported="true"]').count() >= 6);
    assert.equal(await page.getByTestId("runtime-bundled").textContent(), "0");
    assert.match(await page.locator("#static-readiness").textContent(), /Static analysis/i);
    assert.equal(await page.getByTestId("download").isDisabled(), true);
    await page.screenshot({ path: path.join(artifacts, "desktop-real-blocked.png"), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await overflow(page);
    await page.screenshot({ path: path.join(artifacts, "mobile-real-blocked.png"), fullPage: true });

    await page.getByTestId("reset").click();
    await state(page, "idle");
    await page.getByTestId("archive-input").setInputFiles({ name: "cancel.zip", mimeType: "application/zip", buffer: unsupported });
    await page.evaluate(() => {
      document.getElementById("analyze").click();
      document.getElementById("cancel").click();
    });
    await state(page, "cancelled");
    assert.equal(await page.getByTestId("converter").getAttribute("data-worker"), "stopped");
    assert.equal(await page.getByTestId("converter").getAttribute("data-output-bytes"), "0");

    const nativeBinary = path.resolve(__dirname, "../runtime/_build/musl/lilretro");
    try {
      await fs.access(nativeBinary);
      const nativeZip = fixture("native", nativeBinary);
      await analyze(page, nativeZip, "native-elf-qa.zip");
      await state(page, "ready");
      await page.getByTestId("display-name").fill("Native ELF QA");
      await page.getByTestId("version").fill("1.2.3-qa");
      assert.equal(await page.getByTestId("convert").isDisabled(), false);
      await page.getByTestId("convert").click();
      await state(page, "complete");
      const pending = page.waitForEvent("download");
      await page.getByTestId("download").click();
      const download = await pending;
      assert.equal(download.suggestedFilename(), "ui-smoke-1.2.3-qa.lilpck");
      const saved = path.join(artifacts, "native-" + download.suggestedFilename());
      await download.saveAs(saved);
      const inspected = JSON.parse(execFileSync("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path.join(__dirname, "inspect-package.ps1"), "-Path", saved], { encoding: "utf8" }));
      assert.ok(inspected.entries.includes("meta.json"));
      assert.ok(inspected.entries.includes("runtime/port/engine.so"));
      assert.ok(inspected.entries.includes("CONVERSION-REPORT.json"));
      assert.equal(inspected.metadata.name, "Native ELF QA");
      assert.equal(inspected.metadata.version, "1.2.3-qa");
      await page.screenshot({ path: path.join(artifacts, "real-native-package.png"), fullPage: true });
      console.log("PASS: real native MIPS ELF fixture packaged by C/WASM; downloaded ZIP metadata and engine entry verified (no device gameplay claim)");
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
      console.log("SKIP: native device ELF fixture is not built");
    }

    const realCore = path.resolve(__dirname, "../runtime/_build/musl/test_core.so");
    let testZip;
    if (process.env.TF_TEST_ZIP) testZip = await fs.readFile(process.env.TF_TEST_ZIP);
    else { try { await fs.access(realCore); testZip = fixture("libretro", realCore); } catch (_) {} }
    if (testZip) {
      await analyze(page, testZip, "device-core-test.zip");
      const current = await page.getByTestId("converter").getAttribute("data-state");
      if (current === "ready") {
        await page.getByTestId("display-name").fill("Device core QA");
        await page.getByTestId("version").fill("1.2.3-qa");
        await page.getByTestId("convert").click();
        await state(page, "complete");
        assert.ok(Number(await page.getByTestId("converter").getAttribute("data-output-bytes")) > 0);
        const pending = page.waitForEvent("download");
        await page.getByTestId("download").click();
        const download = await pending;
        assert.match(download.suggestedFilename(), /^[A-Za-z0-9_-]+-1\.2\.3-qa\.lilpck$/);
        await download.saveAs(path.join(artifacts, download.suggestedFilename()));
        console.log("PASS: real device test core converted by C/WASM and downloaded");
      } else {
        assert.equal(current, "blocked");
        assert.ok((await page.getByTestId("port-reason").textContent()).trim());
        assert.equal(await page.getByTestId("download").isDisabled(), true);
        console.log("REAL CORE BLOCKED:", await page.getByTestId("port-reason").textContent());
      }
    }
    await page.getByTestId("reset").click();
    await state(page, "idle");
    assert.equal(await page.getByTestId("converter").getAttribute("data-worker"), "stopped");
    assert.equal(await page.getByTestId("converter").getAttribute("data-output-bytes"), "0");
    assert.deepEqual(pageErrors, []);
    const unexpectedRequests = requests.filter((request) => request.method !== "GET" || request.body || (!request.url.startsWith(origin + "/") && !request.url.startsWith("blob:" + origin + "/")));
    assert.deepEqual(unexpectedRequests, [], "no uploads or off-origin requests (local Blob downloads are allowed)");
    console.log("PASS: idle desktop/mobile, extension validation, real blocked ELF analysis, optional runtime fields, cancellation, reset, no uploads/off-origin requests");
    console.log(`Screenshots: ${artifacts}`);
  } finally {
    if (browser) await browser.close();
    await new Promise((resolve) => server.close(resolve));
  }
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
