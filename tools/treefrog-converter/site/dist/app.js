(() => {
  "use strict";

  const MAX_ARCHIVE_BYTES = 402653184;
  const MAX_EXPANDED_BYTES = 805306368;
  const RUNTIME_REQUIRED = "LilSmoke TreeFrog device compatibility runtime";
  const $ = (id) => document.getElementById(id);
  const root = $("app");
  const encoder = new TextEncoder();
  const busyStates = new Set(["reading", "analyzing", "converting"]);
  const stages = ["Layout", "ELF/ABI", "Libretro API exports", "Dependencies", "Launcher", "Rendering / Input / Audio adaptation plan", "Runtime"];
  let selectedFile = null;
  let reader = null;
  let worker = null;
  let report = null;
  let selectedIndex = -1;
  let output = null;
  let runtimeInfo = null;
  let supplementalWarnings = [];
  let dragDepth = 0;

  function text(id, value) { $(id).textContent = value; }
  function busy() { return busyStates.has(root.dataset.state); }
  function port() { return report?.ports?.[selectedIndex] || null; }
  function ready(value) { return value?.ready === true || value?.ready === 1; }
  function display(value) {
    if (value === null || value === undefined || value === "") return "Not reported";
    if (typeof value === "boolean") return value ? "Yes" : "No";
    if (Array.isArray(value)) return value.length ? value.map(display).join("; ") : "None";
    if (typeof value === "object") return Object.entries(value).map(([key, item]) => `${key}: ${display(item)}`).join("; ") || "Not reported";
    return String(value);
  }
  function mib(value) {
    if (!Number.isFinite(value)) return "--";
    if (value < 1024) return `${value.toLocaleString()} B`;
    const divisor = value < 1048576 ? 1024 : 1048576;
    return `${(value / divisor).toLocaleString(undefined, { maximumFractionDigits: 1 })} ${divisor === 1024 ? "KiB" : "MiB"}`;
  }
  function count(value) { return Number.isSafeInteger(value) && value >= 0 ? value.toLocaleString() : "--"; }
  function formatMB(value) { return value > 0 && value < 1000 ? "<0.001" : (value / 1000000).toLocaleString(undefined, { maximumFractionDigits: value < 1000000 ? 3 : 1 }); }
  function list(value) { return Array.isArray(value) ? value : []; }
  function field(key) { return port()?.[key] ?? report?.[key]; }
  function setIcon(name) {
    $("activity-icon").dataset.icon = name;
    window.TreeFrogIcons?.render($("activity-icon").parentElement);
  }
  function engine(state, label) {
    root.dataset.engine = state;
    text("engine-status", `C / WASM / ${label}`);
  }
  function setState(state, message, detail = "") {
    root.dataset.state = state;
    if (message) text("status-message", message);
    text("status-detail", detail);
    $("status-detail").hidden = !detail;
    setIcon(busy() ? "loader-circle" : state === "error" || state === "blocked" ? "triangle-alert" : state === "complete" ? "check" : "cpu");
    updateControls();
  }
  function metadataError() {
    const name = $("display-name").value.trim();
    const version = $("version").value.trim();
    const bad = /[\x00-\x1f\x7f]/;
    const nameBad = !name || bad.test(name) || encoder.encode(name).length > 256;
    const versionBad = !version || bad.test(version) || encoder.encode(version).length > 64;
    $("display-name").setAttribute("aria-invalid", String(!!port() && nameBad));
    $("version").setAttribute("aria-invalid", String(!!port() && versionBad));
    if (nameBad) return "Display name is required (up to 256 UTF-8 bytes; no control characters).";
    if (versionBad) return "Version is required (up to 64 UTF-8 bytes; no control characters).";
    return "";
  }
  function updateControls() {
    const working = busy();
    $("archive-input").disabled = working;
    $("dropzone").setAttribute("aria-disabled", String(working));
    $("analyze").disabled = working || !selectedFile;
    $("port-select").disabled = working || !report?.ports?.length || !worker;
    $("display-name").disabled = working || !port() || !worker;
    $("version").disabled = working || !port() || !worker;
    $("convert").disabled = working || !worker || !ready(port()) || !!metadataError();
    $("download").disabled = working || !output;
    $("cancel").hidden = !working;
    $("reset").disabled = root.dataset.state === "idle";
    root.setAttribute("aria-busy", String(working));
  }
  function progress(stage, processed = 0, total = 0, unit = "bytes") {
    root.dataset.phase = stage;
    text("phase-label", stage);
    const measured = Number.isFinite(total) && total > 0 && Number.isFinite(processed) && processed >= 0;
    const actual = measured ? Math.min(processed, total) : 0;
    const meter = $("progress");
    meter.dataset.processed = String(Math.max(0, Number(processed) || 0));
    meter.dataset.total = String(Math.max(0, Number(total) || 0));
    meter.dataset.unit = unit;
    if (measured) {
      const percent = Math.min(100, (actual / total) * 100);
      meter.value = percent;
      meter.setAttribute("aria-valuetext", `${stage}: ${percent.toFixed(0)}%`);
      text("progress-percent", `${percent.toFixed(0)}%`);
    } else {
      meter.removeAttribute("value");
      meter.setAttribute("aria-valuetext", `${stage}: no byte counter reported`);
      text("progress-percent", "--%");
    }
    text("progress-bytes", measured ? unit === "bytes" ? `${formatMB(actual)} / ${formatMB(total)} MB` : `${count(actual)} / ${count(total)} files` : "-- MB");
  }
  function idleProgress(stage = "Idle") {
    progress(stage);
    $("progress").value = 0;
  }
  function revokeOutput() {
    if (output) URL.revokeObjectURL(output.url);
    output = null;
    root.dataset.outputBytes = "0";
    delete root.dataset.downloaded;
    $("output-summary").hidden = true;
    text("output-name", "");
    text("output-size", "");
  }
  function stopWorker() {
    if (worker) worker.terminate();
    worker = null;
    root.dataset.worker = "stopped";
  }
  function release() {
    const currentReader = reader;
    reader = null;
    if (currentReader) {
      currentReader.onload = currentReader.onerror = currentReader.onprogress = currentReader.onabort = null;
      if (currentReader.readyState === FileReader.LOADING) currentReader.abort();
    }
    stopWorker();
    revokeOutput();
  }
  function reset() {
    release();
    selectedFile = null;
    report = null;
    selectedIndex = -1;
    runtimeInfo = null;
    supplementalWarnings = [];
    root.dataset.selectedPort = "";
    root.dataset.runtime = "unreported";
    $("archive-input").value = "";
    text("file-name", "Choose ZIP archive");
    text("file-meta", "Drop .zip archive");
    text("file-choose-label", "Browse files");
    $("dropzone").classList.remove("is-dragging");
    dragDepth = 0;
    $("port-select").replaceChildren(new Option("No archive analyzed", ""));
    $("display-name").value = "";
    $("version").value = "";
    ["stat-archive", "stat-expanded", "stat-files", "stat-ports"].forEach((id) => text(id, "--"));
    engine("unloaded", "not loaded");
    renderPort();
    idleProgress();
    setState("idle", "Choose an archive to begin.");
  }
  function failure(message, detail = "", engineFailure = false) {
    release();
    if (engineFailure) engine("unavailable", "unavailable");
    else if (root.dataset.engine === "ready") engine("unloaded", "released");
    else if (root.dataset.engine === "loading") engine("unavailable", "unavailable");
    idleProgress("Stopped");
    setState("error", message, detail);
    if (port()) {
      text("port-status", "Worker stopped");
      $("port-status").dataset.status = "error";
      $("port-status").dataset.ready = "false";
    }
  }
  function chooseFiles(files) {
    if (busy()) return;
    reset();
    if (!files || !files.length) return;
    if (files.length !== 1) return failure("Choose one ZIP archive at a time.");
    const file = files[0];
    if (!/\.zip$/i.test(file.name)) return failure("Only .zip archives are accepted.");
    if (!file.size) return failure("The selected archive is empty.");
    if (file.size > MAX_ARCHIVE_BYTES) return failure("Archive exceeds the 384 MiB limit.", "Choose a smaller archive. The file was not read or uploaded; processing stays in this browser.");
    selectedFile = file;
    text("file-name", file.name);
    text("file-meta", `${mib(file.size)} / ZIP archive`);
    text("file-choose-label", "Change archive");
    text("stat-archive", mib(file.size));
    setState("selected", "Archive selected. Ready to analyze.");
  }
  function startWorker() {
    stopWorker();
    const current = new Worker(new URL("./worker.js", document.baseURI));
    worker = current;
    root.dataset.worker = "running";
    current.onmessage = ({ data }) => {
      if (current !== worker || !data || typeof data.type !== "string") return;
      if (data.type === "engine") return engine(data.state, data.label);
      if (data.type === "runtime") {
        runtimeInfo = data;
        root.dataset.runtime = data.state;
        renderRuntime();
        return;
      }
      if (data.type === "progress") return progress(data.stage, data.processed, data.total, data.unit || "bytes");
      if (data.type === "error") return failure(data.message, data.detail || "", data.code === "engine");
      if (data.type === "report") {
        if (!data.report || !Array.isArray(data.report.ports) || data.report.expanded_bytes > MAX_EXPANDED_BYTES) return failure("The engine returned an invalid or over-limit analysis report.");
        report = data.report;
        supplementalWarnings = list(data.warnings);
        selectedIndex = report.ports.length === 1 ? 0 : -1;
        renderArchive();
        populatePorts();
        renderPort();
        progress("Analysis complete", selectedFile.size, selectedFile.size);
        if (!report.ports.length) setState("blocked", "No convertible ports were detected.", "No package was created. Review the engine analysis and warnings.");
        else setState("analyzed", selectedIndex === 0 ? "Port detected automatically. Review the plan and convert." : "Analysis complete. Select a detected port.");
        return;
      }
      if (data.type === "output") {
        if (!(data.buffer instanceof ArrayBuffer) || !data.buffer.byteLength || typeof data.filename !== "string") return failure("The engine returned no valid package output.");
        revokeOutput();
        const blob = new Blob([data.buffer], { type: "application/octet-stream" });
        output = { url: URL.createObjectURL(blob), filename: data.filename, size: blob.size };
        root.dataset.outputBytes = String(blob.size);
        if (data.report) report = data.report;
        renderArchive();
        renderPort(false);
        text("output-name", output.filename);
        text("output-size", mib(output.size));
        $("output-summary").hidden = false;
        progress("Package created", blob.size, blob.size);
        setState("complete", "Package created. Download is ready.", "Static packaging completed. Device compatibility is not verified by conversion.");
      }
    };
    current.onerror = (event) => {
      if (current !== worker) return;
      event.preventDefault();
      failure("The converter worker stopped unexpectedly.", event.message || "Browser memory may be exhausted. Reset and choose a smaller archive; nothing was uploaded.", true);
    };
    current.onmessageerror = () => {
      if (current === worker) failure("The converter worker returned an unreadable response.");
    };
    return current;
  }
  function analyze() {
    if (busy() || !selectedFile) return;
    release();
    report = null;
    selectedIndex = -1;
    runtimeInfo = null;
    supplementalWarnings = [];
    root.dataset.runtime = "unreported";
    populatePorts();
    renderPort();
    ["stat-expanded", "stat-files", "stat-ports"].forEach((id) => text(id, "--"));
    engine("loading", "starting");
    setState("reading", "Reading archive into browser memory.");
    progress("Reading archive", 0, selectedFile.size);
    let current;
    try { current = startWorker(); }
    catch (error) { return failure("This browser could not start the converter worker.", error.message, true); }
    const fileReader = new FileReader();
    reader = fileReader;
    fileReader.onprogress = (event) => {
      if (reader === fileReader) progress("Reading archive", event.loaded, event.lengthComputable ? event.total : 0);
    };
    fileReader.onerror = () => {
      if (reader === fileReader) failure("The browser could not read this archive.", fileReader.error?.message || "Choose the archive again.");
    };
    fileReader.onload = () => {
      if (reader !== fileReader || worker !== current) return;
      const buffer = fileReader.result;
      reader = null;
      if (!(buffer instanceof ArrayBuffer)) return failure("The archive could not be read as binary data.");
      setState("analyzing", "Analyzing with the C/WASM engine.");
      progress("Loading C/WASM engine");
      try { current.postMessage({ type: "analyze", buffer }, [buffer]); }
      catch (error) { failure("The archive could not be transferred to the worker.", error.message); }
    };
    try { fileReader.readAsArrayBuffer(selectedFile); }
    catch (error) { failure("The browser could not allocate memory for this archive.", error.message); }
  }
  function populatePorts() {
    const select = $("port-select");
    const ports = report?.ports || [];
    select.replaceChildren(new Option(report ? ports.length ? "Select a detected port" : "No ports detected" : "No archive analyzed", ""));
    ports.forEach((item, index) => {
      const option = new Option(`${item.name || item.id || `Port ${index + 1}`}${ready(item) ? "" : " [needs adaptation]"}`, String(index));
      option.dataset.ready = String(ready(item));
      select.add(option);
    });
    select.value = selectedIndex >= 0 ? String(selectedIndex) : "";
  }
  function renderArchive() {
    text("stat-archive", mib(report?.archive_bytes));
    text("stat-expanded", mib(report?.expanded_bytes));
    text("stat-files", count(report?.files));
    text("stat-ports", count(report?.ports?.length));
  }
  function renderPort(updateMetadata = true) {
    const item = port();
    root.dataset.selectedPort = item ? String(item.id ?? selectedIndex) : "";
    const status = $("port-status");
    status.dataset.status = item ? ready(item) ? "ready" : "blocked" : "unreported";
    status.dataset.ready = String(ready(item));
    status.textContent = item ? ready(item) ? "Ready to package" : "Needs adaptation" : report?.ports?.length ? "Select a port" : report ? "No ports detected" : "Awaiting analysis";
    if (updateMetadata) {
      $("display-name").value = item ? String(item.name ?? item.id ?? "") : "";
      $("version").value = item ? String(item.version ?? "") : "";
    }
    const binary = item?.binary;
    const architecture = item?.architecture ?? item?.arch ?? (typeof binary === "object" && binary ? binary.architecture ?? binary.arch ?? binary.machine ?? binary.abi : null) ?? item?.abi;
    text("port-architecture", display(architecture));
    text("port-profile", display(item?.profile));
    text("port-binary", display(binary));
    text("port-content", item ? `${display(item.content)} / ${display(item.icon)}` : "Not reported");
    text("port-reason", item?.reason ? display(item.reason) : item ? ready(item) ? "Engine approved packaging. Device compatibility is not verified." : "The engine has not approved this port for conversion." : "No port selected.");
    renderAnalysis();
    renderRuntime();
    renderWarnings();
  }
  function canonicalStage(value) {
    const key = String(value || "").toLowerCase().replace(/[^a-z0-9]/g, "");
    const aliases = {
      layout: 0, archivelayout: 0, elfabi: 1, elf: 1, abi: 1,
      libretroapiexports: 2, libretroexports: 2, libretroapi: 2,
      dependencies: 3, dependencyclosure: 3, launcher: 4,
      renderinginputaudioadaptationplan: 5, adaptationplan: 5, adaptations: 5,
      runtime: 6
    };
    return aliases[key] ?? -1;
  }
  function renderAnalysis() {
    const entries = list(field("analysis"));
    const rows = stages.map((stage) => ({ stage, status: "Not reported", detail: "--", reported: false }));
    for (const entry of entries) {
      if (!entry || typeof entry !== "object") continue;
      const index = canonicalStage(entry.stage);
      const row = { stage: display(entry.stage), status: display(entry.status), detail: display(entry.detail), reported: true };
      if (index >= 0 && !rows[index].reported) rows[index] = row;
      else rows.push(row);
    }
    const body = $("analysis-rows");
    body.replaceChildren();
    for (const entry of rows) {
      const row = document.createElement("tr");
      row.dataset.stage = entry.stage;
      row.dataset.reported = String(entry.reported);
      [entry.stage, entry.status, entry.detail].forEach((value, index) => {
        const cell = document.createElement("td");
        cell.textContent = value;
        if (index === 1) cell.dataset.status = entry.reported ? String(entry.status).toLowerCase() : "unreported";
        row.append(cell);
      });
      body.append(row);
    }
    text("analysis-count", entries.length ? `${entries.length} engine stages` : "Awaiting engine report");
    const readiness = field("readiness");
    const structured = readiness && typeof readiness === "object" && !Array.isArray(readiness);
    const device = structured ? readiness.device_probe ?? readiness.device ?? readiness.device_ready : undefined;
    const staticValue = structured ? readiness.static_analysis ?? readiness.static ?? readiness.static_ready ?? readiness : readiness;
    text("static-readiness", display(staticValue));
    text("device-readiness", display(device));
    renderList("adaptations", list(field("adaptations")));
    $("adaptations-section").hidden = !list(field("adaptations")).length;
    const strategies = list(field("strategies"));
    renderList("strategies", strategies.map(s => `${display(s.stage)}: ${display(s.status)}. ${display(s.detail)}`));
    $("strategies-section").hidden = !strategies.length;
  }
  function renderList(id, entries) {
    const target = $(id);
    target.replaceChildren();
    for (const entry of entries) {
      const li = document.createElement("li");
      li.textContent = display(entry);
      target.append(li);
    }
  }
  function renderRuntime() {
    text("runtime-required", display(field("runtime_required") || RUNTIME_REQUIRED));
    text("runtime-profile", display(field("runtime_profile")));
    text("runtime-abi", display(field("abi")));
    const dependencies = field("dependencies");
    const missing = field("missing_dependencies");
    const conversion = report?.conversion;
    const bundled = conversion?.bundled_count ?? field("bundled_count") ?? field("bundled_dependencies");
    const markedDependencies = Array.isArray(dependencies) && dependencies.length > 0 && dependencies.every((dependency) => dependency && typeof dependency === "object" && typeof dependency.bundled === "boolean");
    text("runtime-bundled", Number.isSafeInteger(bundled) && bundled >= 0 ? count(bundled) : markedDependencies ? count(dependencies.filter((dependency) => dependency.bundled).length) : "Not reported");
    text("runtime-missing-count", Array.isArray(missing) ? count(missing.length) : "Not reported");
    renderList("dependencies", list(dependencies));
    renderList("missing-dependencies", list(missing));
    text("dependency-count", Array.isArray(dependencies) ? `(${dependencies.length})` : "");
    $("dependencies-section").hidden = !Array.isArray(dependencies);
    $("missing-section").hidden = !list(missing).length;
    const status = $("runtime-status");
    status.dataset.status = list(missing).length ? "blocked" : runtimeInfo?.state === "attached" ? "ready" : "unreported";
    status.textContent = list(missing).length ? `${missing.length} missing` : runtimeInfo?.state === "attached" ? "Pack attached" : runtimeInfo?.state === "unavailable" ? "Pack unavailable" : runtimeInfo?.state === "loading" ? "Loading pack" : "Not reported";
    text("runtime-pack-detail", runtimeInfo?.detail || "");
    $("runtime-pack-detail").hidden = !runtimeInfo?.detail;
  }
  function renderWarnings() {
    const entries = [];
    const add = (values, severity = "warning") => list(values).forEach((value) => entries.push({ value: display(value), severity }));
    add(report?.warnings);
    add(port()?.warnings);
    add(report?.compatibility_warnings);
    add(port()?.compatibility_warnings);
    add(report?.conversion?.warnings);
    add(supplementalWarnings);
    add(report?.errors, "error");
    add(port()?.errors, "error");
    add(report?.conversion?.errors, "error");
    const seen = new Set();
    const unique = entries.filter((entry) => {
      const key = `${entry.severity}:${entry.value}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    });
    const target = $("warnings");
    target.replaceChildren();
    unique.forEach((entry) => {
      const li = document.createElement("li");
      li.dataset.severity = entry.severity;
      li.textContent = entry.value;
      target.append(li);
    });
    text("warning-count", `${unique.length} reported`);
  }
  function selectPort() {
    if (busy()) return;
    revokeOutput();
    selectedIndex = $("port-select").value === "" ? -1 : Number($("port-select").value);
    renderPort();
    if (!port()) setState("analyzed", "Select a detected port.");
    else if (!ready(port())) setState("blocked", "Conversion blocked by the C/WASM engine.", display(port().reason || "This port has not passed the engine's safety and compatibility checks."));
    else setState("ready", "Port selected. Ready to package.", metadataError());
  }
  function convert() {
    if (busy() || !worker || !ready(port())) return;
    const error = metadataError();
    if (error) return setState("ready", "Package metadata needs attention.", error);
    revokeOutput();
    setState("converting", "Generating the LilSmoke package.");
    progress("Converting with C/WASM");
    try {
      worker.postMessage({ type: "convert", index: selectedIndex, name: $("display-name").value.trim(), version: $("version").value.trim() });
    } catch (failureError) { failure("Conversion could not start.", failureError.message); }
  }
  $("archive-input").addEventListener("change", (event) => {
    const files = Array.from(event.target.files || []);
    chooseFiles(files);
  });
  $("analyze").addEventListener("click", analyze);
  $("port-select").addEventListener("change", selectPort);
  $("convert").addEventListener("click", convert);
  ["display-name", "version"].forEach((id) => $(id).addEventListener("input", () => {
    revokeOutput();
    if (port()) setState(ready(port()) ? "ready" : "blocked", ready(port()) ? "Package metadata updated." : "Conversion blocked by the C/WASM engine.", ready(port()) ? metadataError() : display(port().reason));
  }));
  $("download").addEventListener("click", () => {
    if (!output || busy()) return;
    const link = document.createElement("a");
    link.href = output.url;
    link.download = output.filename;
    link.hidden = true;
    document.body.append(link);
    link.click();
    link.remove();
    root.dataset.downloaded = "true";
  });
  $("reset").addEventListener("click", reset);
  $("cancel").addEventListener("click", () => {
    reset();
    idleProgress("Cancelled");
    setState("cancelled", "Cancelled. Worker terminated and archive memory released.");
  });
  const dropzone = $("dropzone");
  dropzone.addEventListener("dragenter", (event) => {
    event.preventDefault();
    if (!busy()) { dragDepth += 1; dropzone.classList.add("is-dragging"); }
  });
  dropzone.addEventListener("dragover", (event) => {
    event.preventDefault();
    if (event.dataTransfer) event.dataTransfer.dropEffect = busy() ? "none" : "copy";
  });
  dropzone.addEventListener("dragleave", (event) => {
    event.preventDefault();
    dragDepth = Math.max(0, dragDepth - 1);
    if (!dragDepth) dropzone.classList.remove("is-dragging");
  });
  dropzone.addEventListener("drop", (event) => {
    event.preventDefault();
    dragDepth = 0;
    dropzone.classList.remove("is-dragging");
    if (!busy()) chooseFiles(Array.from(event.dataTransfer?.files || []));
  });
  document.addEventListener("dragover", (event) => event.preventDefault());
  document.addEventListener("drop", (event) => event.preventDefault());
  window.addEventListener("pagehide", release);
  window.addEventListener("pageshow", (event) => {
    if (event.persisted) reset();
  });
  renderPort();
  updateControls();
})();
