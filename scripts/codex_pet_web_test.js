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

function rejectSource(pattern, message) {
  if (pattern.test(script)) throw new Error(message);
}

requireSource(/performance\.getEntriesByType\(["']navigation["']\)/,
  "deep links must distinguish first navigation from refresh");
requireSource(/history\.replaceState\(/,
  "consumed deep-link parameters must be removed from the address bar");
rejectSource(/state\.query\s*=\s*installSlug/,
  "deep-link install targets must not overwrite the user's gallery search");
rejectSource(/\$\(["']search["']\)\.value\s*=\s*installSlug/,
  "deep-link install targets must not fill the search input");
requireSource(/保存 \.hpet/,
  "the optional package action must be labelled as saving an hpet file");
requireSource(/先绑定 Codex/,
  "deploy must explain that Codex binding is required");
requireSource(/Codex Hooks 未信任/,
  "the Companion must surface untrusted Codex hooks as a project warning");
requireSource(/\/hooks/,
  "the untrusted-hooks warning must provide the Codex review command");
requireSource(/codex\?\.bound && codex\?\.trusted === true/,
  "Codex readiness must require both binding and runtime trust");
requireSource(/hookTrustNeedsAction/,
  "the status renderer must distinguish untrusted hooks from an unbound project");
requireSource(/先连接板子/,
  "deploy must explain that the board connection is required");
requireSource(/state\.jobActive \|\| !\(board && codex\)/,
  "deploy must be disabled until binding and board connection are ready");
requireHtml(/id="firmwareUpdate"/, "firmware update control is missing");
requireHtml(/id="firmwareRollback"/, "firmware rollback control is missing");
requireHtml(/id="supportBundle"/, "support bundle control is missing");
requireSource(/\/v1\/firmware\/update/, "firmware update must use the Companion API");
requireSource(/\/v1\/firmware\/rollback/, "firmware rollback must use the Companion API");
requireSource(/\/v1\/support\/bundle/, "support bundle must use the Companion API");
requireSource(/Authorization|authorization/, "support downloads must carry the Companion session");
requireSource(/job\.kind === ["']build["'][\s\S]*保存完成/,
  "build completion must not be reported as board deployment");
requireSource(/response\.status === 401[\s\S]*\/v1\/session/,
  "expired Companion sessions must refresh once");
requireSource(/http:\/\/127[.]0[.]0[.]1:8790/,
  "a public gallery must target the fixed loopback Companion API");
requireSource(/fetch\(companionURL\(path\)/,
  "all Companion API calls must pass through the loopback URL resolver");
requireHtml(/name="vibeboard-companion-download"/,
  "a public gallery must expose a configurable Companion download URL");
requireHtml(/href="vibeboard:\/\/companion\/open"/,
  "an offline public gallery must offer the Companion launch deep link");
requireHtml(/connect-src[^>]*http:\/\/127[.]0[.]0[.]1:8790/,
  "the CSP must allow only the fixed loopback Companion endpoint");
requireSource(/getImageData\(/,
  "sprite animation must inspect frames instead of displaying transparent frames");
requireSource(/data-frames|frameList|visibleFrames/,
  "sprite animation must retain a non-empty frame list per state row");
requireSource(/requestAnimationFrame|setTimeout\(animateSprites/,
  "sprite animation must use a continuous browser animation loop");
for (const [state, label] of [
  ["idle", "待机"], ["runRight", "向右跑"], ["runLeft", "向左跑"],
  ["waving", "挥手"], ["jumping", "跳跃"], ["failed", "失败"],
  ["waiting", "等待"], ["running", "运行"], ["review", "审阅"],
]) {
  requireSource(new RegExp(`${state}: ["']${label}["']`),
    `the Petdex ${state} animation must be exposed as ${label}`);
}
requireHtml(/\.states\s*\{[\s\S]*?grid-template-columns:\s*repeat\(3,\s*minmax\(0,\s*1fr\)\)/,
  "nine Petdex state buttons must use a stable three-column grid");

const visibleFrameFunction = script.match(
  /function visibleSpriteFrames\([\s\S]*?\n    }\n\n    function loadSprite/,
);
if (!visibleFrameFunction) {
  throw new Error("sprite visibility function must be executable in isolation");
}
const functionSource = visibleFrameFunction[0].replace(/\n\n    function loadSprite$/, "");
const visibleSpriteFrames = new Function(
  "SPRITE_COLUMNS",
  `${functionSource}; return visibleSpriteFrames;`,
)(4);

const width = 8;
const height = 4;
const pixels = new Uint8ClampedArray(width * height * 4);
function markVisible(x, y) {
  pixels[(y * width + x) * 4 + 3] = 255;
}
markVisible(0, 0); // Row 0, frame 0.
markVisible(5, 1); // Row 0, frame 2; frames 1 and 3 stay transparent.
markVisible(3, 3); // Row 1, frame 1.
const visible = visibleSpriteFrames(pixels, width, height, 2, 4);
if (JSON.stringify(visible) !== JSON.stringify([[0, 2], [1]])) {
  throw new Error(`transparent sprite columns leaked into animation: ${JSON.stringify(visible)}`);
}
const transparent = visibleSpriteFrames(new Uint8ClampedArray(width * height * 4), width, height, 2, 4);
if (JSON.stringify(transparent) !== JSON.stringify([[0], [0]])) {
  throw new Error(`fully transparent rows need a stable fallback frame: ${JSON.stringify(transparent)}`);
}

process.stdout.write("codex_pet_web self-test ok\n");
