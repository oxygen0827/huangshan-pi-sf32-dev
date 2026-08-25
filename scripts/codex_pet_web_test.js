#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");

const webPath = path.join(__dirname, "codex_pet_web.html");
const html = fs.readFileSync(webPath, "utf8");
const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];

if (!script) throw new Error("Codex Pet web page has no inline script");
new Function(script);

function requireSource(pattern, message) {
  if (!pattern.test(script)) throw new Error(message);
}

function requireHtml(pattern, message) {
  if (!pattern.test(html)) throw new Error(message);
}

function rejectHtml(pattern, message) {
  if (pattern.test(html)) throw new Error(message);
}

function rejectSource(pattern, message) {
  if (pattern.test(script)) throw new Error(message);
}

rejectSource(/\$\(["']search["']\)/,
  "homepage should not include a gallery search input");
rejectSource(/data-deploy/,
  "homepage should not include per-pet deploy buttons");
rejectSource(/data-download/,
  "homepage should not include per-pet download buttons");
rejectSource(/getImageData\(/,
  "homepage should not inspect pet sprite frames");
rejectSource(/requestAnimationFrame/,
  "homepage should not run a sprite animation loop");
rejectSource(/function loadSprite\(/,
  "homepage should not load pet sprites");
rejectSource(/renderPets\(/,
  "homepage should not render a pet gallery");
rejectSource(/\$\(["']gallery["']\)/,
  "homepage should not reference the pet gallery element");
requireSource(/isLoopbackPage/,
  "homepage must detect loopback deployments");
requireSource(/companionPortStart\s*=\s*8790[\s\S]*companionPortEnd\s*=\s*8899/,
  "a public homepage must scan the Companion loopback port range");
requireSource(/function discoverCompanion\([\s\S]*isCompanionStatusPayload/,
  "a public homepage must discover the real Companion instead of trusting a fixed port");
requireSource(/value[?].companion[?].connected === true[\s\S]*value[?].companion[?].version != null/,
  "Companion discovery must validate the status payload identity");
requireSource(/fetch\(companionURL\(path\)/,
  "all Companion API calls must pass through the loopback URL resolver");
requireSource(/response\.status === 401[\s\S]*\/v1\/session/,
  "expired Companion sessions must refresh once");
requireSource(/\/v1\/health/,
  "homepage must consume the Companion health endpoint");
requireSource(/serviceReady/,
  "status rendering must be gated by Companion service readiness");
requireSource(/任务因 Companion 重启而中断/,
  "interrupted persistent jobs need an actionable status label");
requireHtml(/name="vibeboard-companion-download"/,
  "homepage must expose a configurable Companion download URL");
requireHtml(/href="vibeboard:\/\/companion\/open"/,
  "homepage must offer the Companion launch deep link");
requireHtml(/id="companionActions"/, "the companion actions in header are missing");
requireHtml(/id="companionLaunch"/, "the companion launch link in header is missing");
requireHtml(/id="companionDownload"/, "the companion download link in header is missing");
rejectHtml(/id="companionWarning"/, "duplicate companion warning should be removed");
rejectHtml(/id="downloadCompanion"/, "duplicate download companion link should be removed");
requireHtml(/id="onboardingCompanion"/, "the unboxing guide must expose the Companion step");
requireHtml(/id="onboardingBoardStep"/, "the unboxing guide must expose the board step");
requireHtml(/id="onboardingCodexStep"/, "the unboxing guide must expose the Codex step");
requireHtml(/id="onboardingDeployStep"/, "the unboxing guide must expose the final usage step");
requireHtml(/id="heroPrimary"/, "the hero must expose the primary Companion action");
requireSource(/function renderOnboarding\(/,
  "the unboxing guide must render from live Companion status");
requireSource(/const codexReadyAfterBoard = board && codex/,
  "the unboxing guide must not unlock the final step before every prerequisite is ready");
requireSource(/\$\("onboardingBoard"\)\.addEventListener\("click", pairBoard\)/,
  "the board onboarding action must use the existing pairing flow");
requireSource(/\$\("onboardingCodex"\)\.addEventListener\("click", advanceCodexOnboarding\)/,
  "the Codex onboarding action must respect the trust-review flow");
requireSource(/function advanceCodexOnboarding\(/,
  "the unboxing guide must offer /hooks review after a partial Codex binding");
requireSource(/!board \? "等待连接" : \(codex \? "已绑定"/,
  "a locked Codex step must not be visually reported as completed");
requireSource(/codex\?\.bound && codex\?\.trusted === true/,
  "Codex readiness must require both binding and runtime trust");
requireSource(/hookTrustNeedsAction/,
  "the status renderer must distinguish untrusted hooks from an unbound project");
requireSource(/Codex Hooks 未信任/,
  "the Companion must surface untrusted Codex hooks as a project warning");
requireSource(/\/hooks/,
  "the untrusted-hooks warning must provide the Codex review command");
requireHtml(/connect-src[^>]*http:\/\/127[.]0[.]0[.]1:\*/,
  "the CSP must allow dynamically selected loopback Companion ports");

requireHtml(/id="firmwareCheck"/, "firmware check control is missing");
requireHtml(/id="firmwareUpdate"/, "firmware update control is missing");
requireHtml(/id="firmwareRollback"/, "firmware rollback control is missing");
requireHtml(/id="supportBundle"/, "support bundle control is missing");
requireSource(/\/v1\/firmware\/check/, "firmware checks must use the Companion API");
requireSource(/\/v1\/firmware\/update/, "firmware update must use the Companion API");
requireSource(/\/v1\/firmware\/rollback/, "firmware rollback must use the Companion API");
requireSource(/function checkFirmware\(/, "the firmware check control needs a dedicated action");
requireSource(/firmwareCheck\.state !== "update_available"/, "firmware installation must require a successful update check");
requireSource(/firmwareCheck\.canInstall !== true/, "the browser must obey the Companion delivery decision");
requireSource(/\/v1\/support\/bundle/, "support bundle must use the Companion API");
requireSource(/Authorization|authorization/, "support downloads must carry the Companion session");
requireSource(/job\.kind === ["']support_bundle["'][\s\S]*诊断包完成/,
  "support bundle completion must be labelled distinctly");
requireSource(/job\.kind\.startsWith\("firmware_"\)[\s\S]*固件更新完成/,
  "firmware update completion must be labelled distinctly");

process.stdout.write("codex_pet_web self-test ok\n");
