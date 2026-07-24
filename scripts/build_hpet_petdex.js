#!/usr/bin/env node
"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const zlib = require("node:zlib");

const BUNDLED_SHARP_PATH = "/Applications/ChatGPT.app/Contents/Resources/cua_node/lib/node_modules/sharp";
const CELL_WIDTH = 192;
const CELL_HEIGHT = 208;
const OUTPUT_WIDTH = 160;
const OUTPUT_HEIGHT = 173;
const PRELOAD_MAGIC = 0x43504256; // VBPC
const PRELOAD_VERSION = 2;
const PRELOAD_HEADER_SIZE = 16;
const PRELOAD_STATE_ENTRY_SIZE = 12;
const MAX_FRAMES_PER_STATE = 8;
const MIN_FRAMES_PER_STATE = 2;
const FRAME_MS = 120;
const MAX_SOURCE_BYTES = 16 * 1024 * 1024;
const MAX_METADATA_BYTES = 64 * 1024;
const PETDEX_CONTRACT = JSON.parse(fs.readFileSync(path.join(__dirname, "petdex_state_contract.json"), "utf8"));
const SOURCE_STATES = [...PETDEX_CONTRACT.states]
  .sort((left, right) => left.row - right.row)
  .map(state => [state.id, state.row]);

function loadSharp() {
  const candidates = [process.env.CODEX_PET_SHARP, "sharp", BUNDLED_SHARP_PATH].filter(Boolean);
  const errors = [];
  for (const candidate of [...new Set(candidates)]) {
    try {
      return require(candidate);
    } catch (error) {
      errors.push(`${candidate}: ${error.code || error.message}`);
    }
  }
  throw new Error(`Sharp is required for Petdex conversion (${errors.join("; ")})`);
}

function safeField(value, fallback, max = 80) {
  const text = String(value || fallback).replace(/[|\r\n]+/g, " ").replace(/\s+/g, " ").trim();
  return (text || fallback).slice(0, max);
}

function validateSlug(value) {
  const slug = String(value || "");
  if (!/^[a-z0-9][a-z0-9-]{0,23}$/.test(slug)) throw new Error(`invalid Petdex slug: ${slug}`);
  return slug;
}

function validateAssetUrl(value) {
  const url = new URL(String(value || ""));
  if (url.protocol !== "https:" || url.hostname !== "assets.petdex.dev" ||
      url.username || url.password || url.port || url.hash) {
    throw new Error(`Petdex asset URL is not allowlisted: ${url.href}`);
  }
  return url.href;
}

function resolveAssetRedirect(current, location) {
  if (!location) throw new Error("source fetch failed: Petdex redirect has no location");
  return validateAssetUrl(new URL(location, current).href);
}

async function download(url, maxBytes) {
  let current = validateAssetUrl(url);
  let response;
  for (let redirects = 0; redirects <= 4; redirects++) {
    try {
      response = await fetch(current, { redirect: "manual" });
    } catch (error) {
      throw new Error(`source fetch failed: ${error.message}`);
    }
    if (![301, 302, 303, 307, 308].includes(response.status)) break;
    if (redirects === 4) throw new Error("source fetch failed: too many Petdex redirects");
    current = resolveAssetRedirect(current, response.headers.get("location"));
  }
  if (!response?.ok) throw new Error(`source fetch failed (${response?.status || "unknown"}): ${current}`);
  const declared = Number(response.headers.get("content-length") || 0);
  if (declared > maxBytes) throw new Error(`download exceeds ${maxBytes} bytes: ${url}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  if (bytes.length > maxBytes) throw new Error(`download exceeds ${maxBytes} bytes: ${url}`);
  return bytes;
}

async function inputBytes(entry, remoteKey, localKey, maxBytes, allowLocal) {
  if (allowLocal && entry[localKey]) {
    const bytes = fs.readFileSync(path.resolve(entry[localKey]));
    if (bytes.length > maxBytes) throw new Error(`${localKey} exceeds ${maxBytes} bytes`);
    return bytes;
  }
  return download(entry[remoteKey], maxBytes);
}

function rgb565Alpha(data, info) {
  if (info.width !== OUTPUT_WIDTH || info.height !== OUTPUT_HEIGHT || info.channels !== 4) {
    throw new Error(`unexpected converted frame ${info.width}x${info.height}x${info.channels}`);
  }
  const output = Buffer.allocUnsafe(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3);
  for (let source = 0, target = 0; source < data.length; source += 4, target += 3) {
    const sourceAlpha = data[source + 3];
    if (sourceAlpha <= 8) {
      output[target] = 0;
      output[target + 1] = 0;
      output[target + 2] = 0;
      continue;
    }
    const alpha = Math.min(15, Math.round(sourceAlpha / 17)) * 17;
    const rgb565 = ((data[source] & 0xf8) << 8) |
      ((data[source + 1] & 0xfc) << 3) |
      (data[source + 2] >> 3);
    output.writeUInt16LE(rgb565, target);
    output[target + 2] = alpha;
  }
  return output;
}

async function extractStateFrames(sharp, spritesheet, row) {
  const distinct = [];
  const hashes = new Set();
  for (let column = 0; column < 8; column++) {
    const extracted = await sharp(spritesheet)
      .extract({ left: column * CELL_WIDTH, top: row * CELL_HEIGHT, width: CELL_WIDTH, height: CELL_HEIGHT })
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });
    let visible = 0;
    for (let offset = 3; offset < extracted.data.length; offset += 4) {
      if (extracted.data[offset] > 8) visible++;
    }
    if (!visible) continue;
    const resized = await sharp(extracted.data, {
      raw: { width: CELL_WIDTH, height: CELL_HEIGHT, channels: 4 },
    }).resize(OUTPUT_WIDTH, OUTPUT_HEIGHT, { kernel: "nearest" })
      .raw()
      .toBuffer({ resolveWithObject: true });
    const raw = rgb565Alpha(resized.data, resized.info);
    const digest = crypto.createHash("sha256").update(raw).digest("hex");
    if (!hashes.has(digest)) {
      hashes.add(digest);
      distinct.push({ raw, sourceColumn: column });
    }
    if (distinct.length === MAX_FRAMES_PER_STATE) break;
  }
  if (distinct.length < MIN_FRAMES_PER_STATE) {
    throw new Error(`source row ${row} needs at least ${MIN_FRAMES_PER_STATE} visually different frames`);
  }
  return distinct;
}

function buildPreload(states) {
  const stateDirectory = [];
  const compressedStates = [];
  let totalFrames = 0;
  for (const [stateId] of SOURCE_STATES) {
    const payload = Buffer.concat(states[stateId].frames.map(frame => frame.raw));
    const compressed = zlib.deflateSync(payload, { level: 9 });
    stateDirectory.push({
      firstFrame: totalFrames,
      frameCount: states[stateId].frames.length,
      compressed,
    });
    compressedStates.push(compressed);
    totalFrames += states[stateId].frames.length;
  }
  const stateBytes = stateDirectory.length * PRELOAD_STATE_ENTRY_SIZE;
  const header = Buffer.alloc(PRELOAD_HEADER_SIZE + stateBytes);
  header.writeUInt32LE(PRELOAD_MAGIC, 0);
  header.writeUInt16LE(PRELOAD_VERSION, 4);
  header.writeUInt16LE(1, 6);
  header.writeUInt16LE(OUTPUT_WIDTH, 8);
  header.writeUInt16LE(OUTPUT_HEIGHT, 10);
  header.writeUInt16LE(stateDirectory.length, 12);
  header.writeUInt16LE(totalFrames, 14);
  let offset = header.length;
  stateDirectory.forEach((state, index) => {
    const entryOffset = PRELOAD_HEADER_SIZE + index * PRELOAD_STATE_ENTRY_SIZE;
    header.writeUInt16LE(state.firstFrame, entryOffset);
    header.writeUInt8(state.frameCount, entryOffset + 2);
    header.writeUInt32LE(offset, entryOffset + 4);
    header.writeUInt32LE(state.compressed.length, entryOffset + 8);
    offset += state.compressed.length;
  });
  return Buffer.concat([header, ...compressedStates]);
}

function verifyPreloadOrder(preload, states) {
  const stateCount = preload.readUInt16LE(12);
  const totalFrames = preload.readUInt16LE(14);
  if (stateCount !== SOURCE_STATES.length) throw new Error("preload state count mismatch");
  for (let stateIndex = 0; stateIndex < SOURCE_STATES.length; stateIndex++) {
    const [stateId] = SOURCE_STATES[stateIndex];
    const stateOffset = PRELOAD_HEADER_SIZE + stateIndex * PRELOAD_STATE_ENTRY_SIZE;
    const firstFrame = preload.readUInt16LE(stateOffset);
    const frameCount = preload.readUInt8(stateOffset + 2);
    const offset = preload.readUInt32LE(stateOffset + 4);
    const length = preload.readUInt32LE(stateOffset + 8);
    if (frameCount !== states[stateId].frames.length || firstFrame + frameCount > totalFrames) {
      throw new Error(`preload state directory mismatch at ${stateId}`);
    }
    const actual = zlib.inflateSync(preload.subarray(offset, offset + length));
    for (let frame = 0; frame < frameCount; frame++) {
      const expected = states[stateId].frames[frame];
      const frameStart = frame * expected.raw.length;
      if (!actual.subarray(frameStart, frameStart + expected.raw.length).equals(expected.raw)) {
        throw new Error(`preload state order mismatch at ${stateId}#${frame}`);
      }
    }
  }
}

async function convertEntry(entry, outputDir, { allowLocal = false } = {}) {
  const sharp = loadSharp();
  const slug = validateSlug(entry.slug);
  const name = safeField(entry.displayName || entry.name, slug);
  const author = safeField(entry.submittedBy || entry.author, "Petdex creator");
  const [metadataBytes, spritesheet] = await Promise.all([
    inputBytes(entry, "petJsonUrl", "petJsonPath", MAX_METADATA_BYTES, allowLocal),
    inputBytes(entry, "spritesheetUrl", "spritesheetPath", MAX_SOURCE_BYTES, allowLocal),
  ]);
  let sourceMetadata;
  try {
    sourceMetadata = JSON.parse(metadataBytes.toString("utf8"));
  } catch (error) {
    throw new Error(`invalid Petdex pet.json: ${error.message}`);
  }
  if (!sourceMetadata || typeof sourceMetadata !== "object" || Array.isArray(sourceMetadata)) {
    throw new Error("Petdex pet.json must be an object");
  }
  const imageMetadata = await sharp(spritesheet, { limitInputPixels: 1536 * 2288 }).metadata();
  if (imageMetadata.width !== CELL_WIDTH * 8 ||
      ![CELL_HEIGHT * 9, CELL_HEIGHT * 11].includes(imageMetadata.height || 0)) {
    throw new Error(`unsupported Petdex spritesheet: ${imageMetadata.width}x${imageMetadata.height}`);
  }
  const states = {};
  const stateDigests = new Set();
  for (const [stateId, row] of SOURCE_STATES) {
    const frames = await extractStateFrames(sharp, spritesheet, row);
    const digest = crypto.createHash("sha256").update(Buffer.concat(frames.map(frame => frame.raw))).digest("hex");
    if (stateDigests.has(digest)) throw new Error(`${stateId} animation duplicates another required state`);
    stateDigests.add(digest);
    states[stateId] = { sourceRow: row, frames, digest };
  }

  fs.mkdirSync(outputDir, { recursive: true });
  const preload = buildPreload(states);
  verifyPreloadOrder(preload, states);
  const catalog = `VBPETS1\n${slug}|${name}|${author}\n`;
  const preview = await sharp(spritesheet)
    .extract({ left: 0, top: 0, width: CELL_WIDTH, height: CELL_HEIGHT })
    .resize(320, 346, { kernel: "nearest" })
    .webp({ lossless: true })
    .toBuffer();
  fs.writeFileSync(path.join(outputDir, "catalog.txt"), catalog);
  fs.writeFileSync(path.join(outputDir, "preload.bin"), preload);
  fs.writeFileSync(path.join(outputDir, "preview.webp"), preview);
  const result = {
    slug,
    name,
    author,
    license: safeField(entry.license, "unspecified", 120),
    sourceUrl: String(entry.sourceUrl || `https://petdex.dev/pets/${slug}`),
    petJsonUrl: entry.petJsonUrl || null,
    spritesheetUrl: entry.spritesheetUrl || null,
    sourceSha256: crypto.createHash("sha256").update(spritesheet).digest("hex"),
    sourceDimensions: [imageMetadata.width, imageMetadata.height],
    outputDimensions: [OUTPUT_WIDTH, OUTPUT_HEIGHT],
    preloadVersion: PRELOAD_VERSION,
    compression: "zlib-state-block",
    stateCount: SOURCE_STATES.length,
    totalFrames: SOURCE_STATES.reduce((total, [stateId]) => total + states[stateId].frames.length, 0),
    maxFramesPerState: Math.max(...SOURCE_STATES.map(([stateId]) => states[stateId].frames.length)),
    frameMs: FRAME_MS,
    taskStates: { ...PETDEX_CONTRACT.taskStates },
    states: Object.fromEntries(SOURCE_STATES.map(([stateId]) => [stateId, {
      row: states[stateId].sourceRow,
      frameCount: states[stateId].frames.length,
      columns: states[stateId].frames.map(frame => frame.sourceColumn),
      sha256: states[stateId].digest,
    }])),
  };
  fs.writeFileSync(path.join(outputDir, "conversion.json"), JSON.stringify(result, null, 2) + "\n");
  return result;
}

async function selfTest() {
  const sharp = loadSharp();
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "hpet-converter-"));
  try {
    for (const unsafe of [
      "https://user:secret@assets.petdex.dev/pet.webp",
      "https://assets.petdex.dev:444/pet.webp",
    ]) {
      try {
        validateAssetUrl(unsafe);
        throw new Error(`unsafe Petdex asset URL passed: ${unsafe}`);
      } catch (error) {
        if (String(error.message).startsWith("unsafe Petdex asset URL passed")) throw error;
      }
    }
    try {
      resolveAssetRedirect("https://assets.petdex.dev/pet.webp", "https://example.com/pet.webp");
      throw new Error("unsafe Petdex redirect passed");
    } catch (error) {
      if (error.message === "unsafe Petdex redirect passed") throw error;
    }
    const width = CELL_WIDTH * 8;
    const height = CELL_HEIGHT * 9;
    const pixels = Buffer.alloc(width * height * 4);
    for (let row = 0; row < 9; row++) {
      for (let column = 0; column < 8; column++) {
        const left = column * CELL_WIDTH + 22 + column * 2;
        const top = row * CELL_HEIGHT + 44;
        for (let y = top; y < top + 96; y++) {
          for (let x = left; x < Math.min(left + 76, (column + 1) * CELL_WIDTH); x++) {
            const offset = (y * width + x) * 4;
            pixels[offset] = 35 + row * 38;
            pixels[offset + 1] = 210 - row * 21;
            pixels[offset + 2] = 60 + column * 17;
            pixels[offset + 3] = 255;
          }
        }
      }
    }
    const sheet = path.join(root, "sheet.webp");
    const metadata = path.join(root, "pet.json");
    await sharp(pixels, { raw: { width, height, channels: 4 } }).webp({ lossless: true }).toFile(sheet);
    fs.writeFileSync(metadata, JSON.stringify({ id: "test-pet", name: "Test Pet" }));
    const output = path.join(root, "output");
    const result = await convertEntry({
      slug: "test-pet",
      displayName: "Test Pet",
      submittedBy: "VibeBoard",
      sourceUrl: "https://petdex.dev/pets/test-pet",
      petJsonPath: metadata,
      spritesheetPath: sheet,
    }, output, { allowLocal: true });
    if (result.preloadVersion !== 2 || result.stateCount !== 9 || result.totalFrames !== 72 ||
        result.maxFramesPerState !== 8 || result.frameMs !== 120 ||
        fs.statSync(path.join(output, "preload.bin")).size <= 100) {
      throw new Error("converter did not produce a valid preload");
    }
    for (const [stateId, row] of SOURCE_STATES) {
      if (result.states[stateId]?.row !== row || result.states[stateId]?.frameCount !== 8) {
        throw new Error(`converter mapped ${stateId} to the wrong Petdex animation`);
      }
    }
    if (JSON.stringify(result.taskStates) !== JSON.stringify(PETDEX_CONTRACT.taskStates)) {
      throw new Error("converter did not preserve the task-state contract");
    }
    process.stdout.write("build_hpet_petdex self-test ok\n");
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === "--self-test") return selfTest();
  const entryIndex = args.indexOf("--entry");
  const outputIndex = args.indexOf("--output");
  if (entryIndex < 0 || outputIndex < 0 || !args[entryIndex + 1] || !args[outputIndex + 1]) {
    throw new Error("usage: build_hpet_petdex.js --entry entry.json --output output-dir [--allow-local]");
  }
  const entry = JSON.parse(fs.readFileSync(path.resolve(args[entryIndex + 1]), "utf8"));
  const result = await convertEntry(entry, path.resolve(args[outputIndex + 1]), {
    allowLocal: args.includes("--allow-local"),
  });
  process.stdout.write(JSON.stringify(result) + "\n");
}

main().catch(error => {
  process.stderr.write(`build_hpet_petdex: ${error.message}\n`);
  process.exitCode = 1;
});
