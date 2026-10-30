"use strict";

const MAX_ARCHIVE_BYTES = 402653184;
const MAX_EXPANDED_BYTES = 805306368;
const MAX_WASM_BYTES = 1610612736;
const MAX_RUNTIME_BYTES = 67108864;
const REQUIRED_EXPORTS = ["_malloc", "_free", "_tf_open", "_tf_report", "_tf_convert", "_tf_output_data", "_tf_output_size", "_tf_error", "_tf_close", "UTF8ToString", "stringToUTF8", "lengthBytesUTF8"];
let engine = null;
let archivePtr = 0;
let runtimePtr = 0;
let openAttempted = false;
let archiveOpen = false;
let working = false;
let currentReport = null;
let diagnostics = "";
let runtimeWarnings = [];
let progressPhase = "Analyzing archive";
let lastProgress = 0;

function send(type, values = {}, transfer = []) { self.postMessage({ type, ...values }, transfer); }
function fail(message, code = "conversion", detail = "") {
  const error = new Error(message);
  error.code = code;
  error.detail = detail;
  throw error;
}
function memoryGuard() {
  if (!engine?.HEAPU8 || engine.HEAPU8.byteLength > MAX_WASM_BYTES) fail("The C/WASM engine exceeded the 1,536 MiB memory ceiling.", "memory", "No package was created. The archive limit is 384 MiB and the expanded-data limit is 768 MiB. Nothing was uploaded.");
}
function nativeError(fallback) {
  try {
    const ptr = engine?._tf_error();
    if (ptr) return engine.UTF8ToString(ptr).slice(0, 8192) || fallback;
  } catch (_) { /* A trapped engine may no longer expose its error string. */ }
  return fallback;
}
function emitProgress(stage, processed = 0, total = 0, force = true, unit = "bytes") {
  const now = performance.now();
  if (!force && now - lastProgress < 60 && processed !== total) return;
  lastProgress = now;
  send("progress", { stage: String(stage).slice(0, 160), processed: Number(processed) || 0, total: Number(total) || 0, unit });
}
function cProgress(stage, processed, total) {
  let label = progressPhase;
  let unit = "bytes";
  if (stage === 1) { label = "Analyzing port entries"; unit = "files"; }
  else if (stage === 2) label = "Validating archive CRCs";
  else if (stage === 3) label = "Package finalized";
  if (typeof stage === "string" && stage) label = stage;
  else if (Number.isInteger(stage) && stage > 4096 && engine) {
    try { label = engine.UTF8ToString(stage) || label; } catch (_) { /* Use the active phase for an unknown stage code. */ }
  }
  emitProgress(label, processed, total, false, unit);
}
async function loadEngine() {
  send("engine", { state: "loading", label: "loading engine" });
  if (typeof WebAssembly !== "object") fail("This browser does not support WebAssembly.", "engine");
  try { importScripts("./converter.js"); }
  catch (error) { fail("The C/WASM engine could not be loaded.", "engine", "converter.js and its WASM binary must be available alongside this tool. No archive was uploaded and no package was created."); }
  if (typeof createTreeFrogConverter !== "function") fail("The C/WASM factory createTreeFrogConverter is unavailable.", "engine");
  try {
    engine = await createTreeFrogConverter({
      locateFile: (path) => new URL(path, self.location.href).href,
      ontfprogress: cProgress,
      print: () => {},
      printErr: (message) => { diagnostics = String(message).slice(0, 4096); },
      onMemoryGrowth: () => { if (engine) memoryGuard(); }
    });
  } catch (error) { fail("The C/WASM engine could not initialize.", "engine", String(error.message || error).slice(0, 4096)); }
  for (const name of REQUIRED_EXPORTS) {
    if (typeof engine[name] !== "function") fail(`The C/WASM engine is missing ${name}.`, "engine");
  }
  engine.ontfprogress = cProgress;
  memoryGuard();
  send("engine", { state: "ready", label: "engine ready" });
}
function allocate(size) {
  memoryGuard();
  const ptr = engine._malloc(size);
  memoryGuard();
  if (!Number.isSafeInteger(ptr) || ptr <= 0 || ptr > engine.HEAPU8.byteLength - size) fail("The browser could not allocate enough WASM memory.", "memory", "The memory ceiling is 1,536 MiB. Choose a smaller archive or release other browser memory. No file was uploaded.");
  return ptr;
}
function readReport() {
  memoryGuard();
  const ptr = engine._tf_report();
  if (!Number.isSafeInteger(ptr) || ptr <= 0 || ptr >= engine.HEAPU8.byteLength) fail(nativeError("The C/WASM engine returned no analysis report."));
  let result;
  try { result = JSON.parse(engine.UTF8ToString(ptr)); }
  catch (_) { fail("The C/WASM engine returned an unreadable analysis report."); }
  if (!result || !Array.isArray(result.ports) || result.ports.some((port) => !port || typeof port !== "object")) fail("The C/WASM engine returned an invalid port list.");
  for (const key of ["files", "expanded_bytes", "archive_bytes"]) {
    if (!Number.isSafeInteger(result[key]) || result[key] < 0) fail(`The C/WASM report has an invalid ${key} value.`);
  }
  if (result.archive_bytes > MAX_ARCHIVE_BYTES) fail("Archive exceeds the 384 MiB limit.", "limit");
  if (result.expanded_bytes > MAX_EXPANDED_BYTES) fail("Expanded archive exceeds the 768 MiB limit.", "limit", "No package was created. Choose a smaller archive; all processing stays in this browser.");
  return result;
}
function cleanup() {
  if (engine) {
    // Close the native session before freeing buffers that C may still reference.
    try { if (openAttempted) engine._tf_close(); } catch (_) {}
    try { if (archivePtr) engine._free(archivePtr); } catch (_) {}
    try { if (runtimePtr) engine._free(runtimePtr); } catch (_) {}
  }
  archivePtr = runtimePtr = 0;
  openAttempted = archiveOpen = false;
  currentReport = null;
}
async function runtimeBytes(response) {
  const advertised = Number(response.headers.get("content-length")) || 0;
  if (advertised > MAX_RUNTIME_BYTES) fail("The static runtime pack exceeds the C engine's 64 MiB limit.", "runtime");
  if (!response.body?.getReader) {
    const buffer = await response.arrayBuffer();
    if (buffer.byteLength > MAX_RUNTIME_BYTES) fail("The static runtime pack exceeds the C engine's 64 MiB limit.", "runtime");
    return new Uint8Array(buffer);
  }
  const stream = response.body.getReader();
  const chunks = [];
  let loaded = 0;
  try {
    for (;;) {
      const { value, done } = await stream.read();
      if (done) break;
      loaded += value.byteLength;
      if (loaded > MAX_RUNTIME_BYTES) fail("The static runtime pack exceeds the C engine's 64 MiB limit.", "runtime");
      chunks.push(value);
      emitProgress("Loading static runtime pack", loaded, advertised, false);
    }
  } finally { await stream.cancel().catch(() => {}); }
  const bytes = new Uint8Array(loaded);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
  return bytes;
}
async function attachRuntime() {
  if (typeof engine._tf_runtime !== "function") {
    send("runtime", { state: "unavailable", detail: "This engine does not expose static runtime-pack attachment. Runtime readiness is determined by the C report." });
    return;
  }
  send("runtime", { state: "loading", detail: "Loading the static LilSmoke runtime pack." });
  emitProgress("Loading static runtime pack");
  let bytes;
  try {
    // This request only reads a same-origin static asset. The input archive is never sent.
    const response = await fetch(new URL("./runtime/runtime.zip", self.location.href), { method: "GET", credentials: "same-origin", redirect: "error", referrerPolicy: "no-referrer" });
    if (!response.ok) throw new Error(`Static runtime pack unavailable (HTTP ${response.status}).`);
    bytes = await runtimeBytes(response);
    if (!bytes.byteLength) throw new Error("The static runtime pack is empty.");
  } catch (error) {
    const detail = `${String(error.message || error).slice(0, 1024)} C/WASM will determine which ports are blocked without it.`;
    runtimeWarnings.push(detail);
    send("runtime", { state: "unavailable", detail });
    return;
  }
  runtimePtr = allocate(bytes.byteLength);
  engine.HEAPU8.set(bytes, runtimePtr);
  const size = bytes.byteLength;
  bytes = null;
  progressPhase = "Attaching runtime pack";
  emitProgress(progressPhase);
  const accepted = engine._tf_runtime(runtimePtr, size);
  memoryGuard();
  if (accepted !== 1) {
    const detail = nativeError("The C/WASM engine did not accept the static runtime pack.");
    runtimeWarnings.push(detail);
    send("runtime", { state: "unavailable", detail });
    return;
  }
  send("runtime", { state: "attached", bytes: size, detail: "Static runtime pack attached. Dependency closure and packaging readiness come from the C/WASM report." });
}
async function analyze(buffer) {
  if (!(buffer instanceof ArrayBuffer) || !buffer.byteLength) fail("No archive data was received.");
  if (buffer.byteLength > MAX_ARCHIVE_BYTES) fail("Archive exceeds the 384 MiB limit.", "limit");
  if (engine || archiveOpen) fail("Reset the current archive before opening another.");
  await loadEngine();
  progressPhase = "Copying archive to WASM";
  emitProgress(progressPhase, 0, buffer.byteLength);
  archivePtr = allocate(buffer.byteLength);
  // One copy from the transferred buffer into the native heap; keep its pointer until close.
  engine.HEAPU8.set(new Uint8Array(buffer), archivePtr);
  const size = buffer.byteLength;
  buffer = null;
  emitProgress(progressPhase, size, size);
  progressPhase = "Analyzing archive";
  emitProgress(progressPhase);
  openAttempted = true;
  if (engine._tf_open(archivePtr, size) !== 1) fail(nativeError("The C/WASM engine rejected this archive."));
  archiveOpen = true;
  currentReport = readReport();
  await attachRuntime();
  currentReport = readReport();
  send("report", { report: currentReport, warnings: runtimeWarnings });
}
function utf8(value, limit, label) {
  if (typeof value !== "string" || !value.trim() || /[\x00-\x1f\x7f]/.test(value)) fail(`${label} is required and cannot contain control characters.`);
  const length = engine.lengthBytesUTF8(value);
  if (length > limit) fail(`${label} exceeds its ${limit}-byte UTF-8 limit.`);
  const ptr = allocate(length + 1);
  try { engine.stringToUTF8(value, ptr, length + 1); }
  catch (error) { engine._free(ptr); throw error; }
  return ptr;
}
function safePart(value, fallback) {
  return String(value || "").normalize("NFKD").replace(/[^A-Za-z0-9._-]+/g, "-").replace(/^[._-]+|[._-]+$/g, "").slice(0, 80).replace(/[._-]+$/g, "") || fallback;
}
function convert(data) {
  if (!engine || !archiveOpen || !currentReport) fail("Analyze an archive before converting.");
  if (!Number.isSafeInteger(data.index) || data.index < 0 || data.index >= currentReport.ports.length) fail("Select a valid detected port.");
  // Readiness and adaptation policies belong to C, never to a JavaScript ZIP implementation.
  currentReport = readReport();
  const port = currentReport.ports[data.index];
  if (!port || (port.ready !== true && port.ready !== 1)) fail(String(port?.reason || "The C/WASM engine blocked this port."), "blocked");
  let namePtr = 0;
  let versionPtr = 0;
  try {
    namePtr = utf8(data.name, 256, "Display name");
    versionPtr = utf8(data.version, 64, "Version");
    progressPhase = "Converting port";
    emitProgress(progressPhase);
    if (engine._tf_convert(data.index, namePtr, versionPtr) !== 1) fail(nativeError("The C/WASM conversion failed. No package was created."));
    memoryGuard();
    currentReport = readReport();
    const ptr = engine._tf_output_data();
    const size = engine._tf_output_size();
    memoryGuard();
    if (!Number.isSafeInteger(ptr) || ptr <= 0 || !Number.isSafeInteger(size) || size <= 0 || size > MAX_WASM_BYTES || ptr > engine.HEAPU8.byteLength - size) fail("The C/WASM engine returned invalid or empty package bytes.");
    const output = engine.HEAPU8.slice(ptr, ptr + size);
    const filename = `${safePart(port.id, "port")}-${safePart(data.version, "version")}.lilpck`;
    send("output", { buffer: output.buffer, filename, report: currentReport }, [output.buffer]);
  } finally {
    if (namePtr) engine._free(namePtr);
    if (versionPtr) engine._free(versionPtr);
  }
}
self.onmessage = async ({ data }) => {
  if (!data || working) return;
  // Termination from the main thread is the cancellation primitive for synchronous native work.
  working = true;
  try {
    if (data.type === "analyze") {
      const task = analyze(data.buffer);
      data.buffer = null;
      await task;
    } else if (data.type === "convert") convert(data);
    else fail("Unknown converter worker command.");
  } catch (error) {
    const memoryFailure = error instanceof RangeError || /out of memory|memory access|allocation|memory growth/i.test(String(error.message));
    const message = memoryFailure ? "Browser/WASM memory was exhausted. No package was created." : String(error.message || error).slice(0, 8192);
    const detail = error.detail || (memoryFailure ? "Maximum: 384 MiB archive, 768 MiB expanded, 1,536 MiB WASM heap. Reset and choose a smaller archive. Nothing was uploaded." : diagnostics);
    cleanup();
    send("error", { code: error.code || (memoryFailure ? "memory" : "conversion"), message, detail });
    self.close();
  } finally { working = false; }
};
