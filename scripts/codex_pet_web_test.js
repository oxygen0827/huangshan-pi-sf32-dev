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
requireSource(/state\.jobActive \|\| !\(board && codex && serviceReady\)/,
  "deploy must be disabled until binding and board connection are ready");
requireSource(/\/v1\/health/,
  "the gallery must consume the Companion health endpoint");
requireSource(/\/v1\/progress/,
  "the Companion must consume the local pet progress endpoint");
requireSource(/\/v1\/preferences/,
  "sound settings must use the authenticated preferences endpoint");
requireSource(/\/v1\/audio\/test/,
  "the sound test control must use the explicit audio test endpoint");
requireHtml(/id="progressLevel"/, "the Companion progress level is missing");
requireHtml(/id="weekChart"/, "the seven-day usage chart is missing");
requireHtml(/id="badges"/, "the achievement view is missing");
requireHtml(/id="achievementList"/, "the complete achievement record is missing");
requireHtml(/id="earnedAchievements"/, "earned achievements need their own collection");
requireSource(/const pending = achievements[.]filter\(item => !item[.]unlockedAt\)/,
  "only pending achievements should retain a live progress counter");
requireSource(/获得于/, "earned achievements should show their unlock date instead of live totals");
requireHtml(/id="currentPetSprite"/, "the home page must show the deployed pet animation");
requireHtml(/id="nicknameInput"[^>]*maxlength="24"/, "pet nickname customization needs a bounded input");
requireHtml(/data-nav="home"[^>]*href="\/"/, "the current-pet home route is missing");
requireHtml(/data-nav="pets"[^>]*href="\/pets"/, "the standalone pet gallery route is missing");
if (html.indexOf('class="maintenance home-page"') >= html.indexOf('class="current-pet home-page"')) {
  throw new Error("device maintenance should appear before the current pet content");
}
requireSource(/isPetsPage \? loadPets\(\) : Promise[.]resolve\(\)/,
  "the home page must not load the full Petdex gallery");
requireSource(/\/v1\/current-pet/, "pet nickname customization must use the authenticated current-pet API");
requireSource(/renderAchievements\(active\)/, "the home page must render achievement progress records");
requireHtml(/id="soundVolume"[^>]*min="0"[^>]*max="15"/,
  "sound volume must preserve the board's 0-15 boundary");
requireHtml(/id="quietStart"[^>]*value="22:00"/, "default quiet-hour start is missing");
requireHtml(/id="quietEnd"[^>]*value="08:00"/, "default quiet-hour end is missing");
requireSource(/serviceReady/,
  "deploy must be gated by Companion service readiness");
requireSource(/任务因 Companion 重启而中断/,
  "interrupted persistent jobs need an actionable status label");
requireHtml(/id="firmwareCheck"/, "firmware check control is missing");
requireHtml(/id="firmwareBaseline"/, "baseline firmware control is missing");
requireHtml(/id="firmwareUpdate"/, "firmware update control is missing");
requireHtml(/id="firmwareRollback"/, "firmware rollback control is missing");
requireHtml(/id="supportBundle"/, "support bundle control is missing");
requireSource(/\/v1\/firmware\/check/, "firmware checks must use the Companion API");
requireSource(/\/v1\/firmware\/update/, "firmware update must use the Companion API");
requireSource(/\/v1\/firmware\/baseline/, "baseline firmware install must use the Companion API");
requireSource(/\/v1\/firmware\/rollback/, "firmware rollback must use the Companion API");
requireSource(/function checkFirmware\(/, "the firmware check control needs a dedicated action");
requireSource(/firmwareCheck\.state !== "update_available"/, "firmware installation must require a successful update check");
requireSource(/firmwareCheck\.canInstall !== true/, "the browser must obey the Companion delivery decision");
requireSource(/firmwareCheck\.state !== "current_version_unknown"/, "baseline installation must require an unknown legacy version");
requireSource(/firmwareCheck\.canInstallBaseline !== true/, "baseline installation must require Companion USB readiness");
requireSource(/INSTALL_BASELINE_FIRMWARE/, "baseline installation must require an explicit confirmation token");
requireSource(/window\.confirm\(/, "baseline installation must require browser confirmation");
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
requireSource(/function companionFetchOptions\(/,
  "public Companion requests must use a dedicated local-network fetch policy");
requireSource(/targetAddressSpace:\s*["']loopback["']/,
  "public Companion requests must declare the loopback target address space for Safari");
requireSource(/fetch\(companionURL\(path\),\s*companionFetchOptions\(/,
  "all Companion fetches must apply the local-network fetch policy");
requireHtml(/id="openLocalCompanion"[^>]*href="http:\/\/127[.]0[.]0[.]1:8790\/"[^>]*hidden/,
  "Safari must offer a hidden local Companion fallback without weakening public-page security");
requireHtml(/id="companionWarningTitle"/, "the Companion warning title must support an actionable Safari fallback");
requireHtml(/id="companionWarningDetail"/, "the Companion warning detail must support an actionable Safari fallback");
requireSource(/const isSafariBrowser =/,
  "the public page must distinguish Safari before presenting the local-page fallback");
requireSource(/function renderCompanionUnavailable\(/,
  "Companion startup failures need a dedicated browser-aware renderer");
requireSource(/const useLocalPage = !isLoopbackPage && isSafariBrowser/,
  "the local-page fallback must be limited to public pages in Safari");
requireSource(/\$\("openLocalCompanion"\)\.hidden = !useLocalPage/,
  "the Safari local-page fallback must remain hidden in other environments");
requireSource(/catch \(error\) \{[\s\S]*renderCompanionUnavailable\(\)/,
  "Companion connection failures must render the Safari fallback");
requireHtml(/name="vibeboard-companion-download"/,
  "a public gallery must expose a configurable Companion download URL");
requireHtml(/name="vibeboard-companion-download" content=""/,
  "the source gallery must not expose an unvalidated Companion build");
requireHtml(/href="vibeboard:\/\/companion\/open"/,
  "an offline public gallery must offer the Companion launch deep link");
requireHtml(/id="onboardingCompanion"/, "the unboxing guide must expose the Companion step");
requireHtml(/id="onboardingBoardStep"/, "the unboxing guide must expose the board step");
requireHtml(/id="onboardingCodexStep"/, "the unboxing guide must expose the Codex step");
requireHtml(/id="onboardingDeployStep"/, "the unboxing guide must expose the deployment step");
requireHtml(/id="onboardingDownload"/, "the unboxing guide must expose the Companion download action");
requireHtml(/a[.]compact-button\[hidden\]\s*\{\s*display:\s*none;/,
  "hidden Companion links must not be revived by the compact-button display rule");
requireSource(/function renderOnboarding\(/,
  "the unboxing guide must render from live Companion status");
requireSource(/const codexReadyAfterBoard = board && codex[\s\S]*const deployReady = companion && codexReadyAfterBoard/,
  "the unboxing guide must not unlock deployment before every prerequisite is ready");
requireSource(/\$\("onboardingBoard"\)\.addEventListener\("click", pairBoard\)/,
  "the board onboarding action must use the existing pairing flow");
requireSource(/\$\("onboardingCodex"\)\.addEventListener\("click", advanceCodexOnboarding\)/,
  "the Codex onboarding action must respect the trust-review flow");
requireSource(/function advanceCodexOnboarding\(/,
  "the unboxing guide must offer /hooks review after a partial Codex binding");
requireSource(/!board \? "等待连接" : \(codex \? "已绑定"/,
  "a locked Codex step must not be visually reported as completed");
requireSource(/\$\("onboardingGallery"\)\.addEventListener\("click"[\s\S]*location[.]href = "\/pets"/,
  "the final onboarding action must navigate to the standalone pet gallery");
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

const companionFetchOptionsFunction = script.match(
  /function companionFetchOptions\([^\n]*\) \{\n[\s\S]*?\n    }/,
);
if (!companionFetchOptionsFunction) {
  throw new Error("Companion fetch policy must be executable in isolation");
}
const companionFetchOptionsSource = companionFetchOptionsFunction[0];
const companionFetchOptions = new Function(
  `${companionFetchOptionsSource}; return companionFetchOptions;`,
)();
const publicFetchOptions = companionFetchOptions({ cache: "no-store" }, false);
if (publicFetchOptions.targetAddressSpace !== "loopback" || publicFetchOptions.cache !== "no-store") {
  throw new Error(`public Companion fetch policy is incomplete: ${JSON.stringify(publicFetchOptions)}`);
}
const localFetchOptions = companionFetchOptions({ cache: "no-store" }, true);
if ("targetAddressSpace" in localFetchOptions || localFetchOptions.cache !== "no-store") {
  throw new Error(`local Companion fetch policy must remain same-origin: ${JSON.stringify(localFetchOptions)}`);
}

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
