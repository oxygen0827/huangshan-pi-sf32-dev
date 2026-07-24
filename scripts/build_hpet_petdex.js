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
const PRELOAD_VERSION = 1;
const PRELOAD_HEADER_SIZE = 16;
const FRAMES_PER_STATE = 2;
const FRAME_MS = 180;
const MAX_SOURCE_BYTES = 16 * 1024 * 1024;
const MAX_METADATA_BYTES = 64 * 1024;
const SOURCE_STATES = [
  ["idle", "idle", 0],
  ["running", "run", 2],
  ["ready", "wave", 1],
  ["needs", "review", 4],
  ["blocked", "failed", 3],
];
// Board asset indexes are fixed by vb_pet_asset_state_index():
// idle=0, ready=1, blocked/error=2, needs=3, running=4.
const PRELOAD_ORDER = ["idle", "ready", "blocked", "needs", "running"];

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
  if (url.protocol !== "https:" || url.hostname !== "assets.petdex.dev") {
    throw new Error(`Petdex asset URL is not allowlisted: ${url.href}`);
  }
  return url.href;
}

async function download(url, maxBytes) {
  const response = await fetch(validateAssetUrl(url), { redirect: "follow" });
  if (!response.ok) throw new Error(`download failed (${response.status}): ${url}`);
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
    const rgb565 = ((data[source] & 0xf8) << 8) |
      ((data[source + 1] & 0xfc) << 3) |
      (data[source + 2] >> 3);
    output.writeUInt16LE(rgb565, target);
    output[target + 2] = data[source + 3];
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
    if (distinct.length === FRAMES_PER_STATE) break;
  }
  if (distinct.length < FRAMES_PER_STATE) {
    throw new Error(`source row ${row} needs two visually different frames`);
  }
  return distinct;
}

function buildPreload(states) {
  const compressed = [];
  for (const boardState of PRELOAD_ORDER) {
    for (const frame of states[boardState].frames) {
      compressed.push(zlib.deflateSync(frame.raw, { level: 9 }));
    }
  }
  const header = Buffer.alloc(PRELOAD_HEADER_SIZE + compressed.length * 8);
  header.writeUInt32LE(PRELOAD_MAGIC, 0);
  header.writeUInt16LE(PRELOAD_VERSION, 4);
  header.writeUInt16LE(1, 6);
  header.writeUInt16LE(OUTPUT_WIDTH, 8);
  header.writeUInt16LE(OUTPUT_HEIGHT, 10);
  header.writeUInt16LE(SOURCE_STATES.length, 12);
  header.writeUInt16LE(FRAMES_PER_STATE, 14);
  let offset = header.length;
  compressed.forEach((frame, index) => {
    header.writeUInt32LE(offset, PRELOAD_HEADER_SIZE + index * 8);
    header.writeUInt32LE(frame.length, PRELOAD_HEADER_SIZE + index * 8 + 4);
    offset += frame.length;
  });
  return Buffer.concat([header, ...compressed]);
}

function verifyPreloadOrder(preload, states) {
  let entry = 0;
  for (const boardState of PRELOAD_ORDER) {
    for (const expected of states[boardState].frames) {
      const offset = preload.readUInt32LE(PRELOAD_HEADER_SIZE + entry * 8);
      const length = preload.readUInt32LE(PRELOAD_HEADER_SIZE + entry * 8 + 4);
      const actual = zlib.inflateSync(preload.subarray(offset, offset + length));
      if (!actual.equals(expected.raw)) throw new Error(`preload state order mismatch at ${boardState}#${entry % 2}`);
      entry += 1;
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
  for (const [boardState, sourceState, row] of SOURCE_STATES) {
    const frames = await extractStateFrames(sharp, spritesheet, row);
    const digest = crypto.createHash("sha256").update(Buffer.concat(frames.map(frame => frame.raw))).digest("hex");
    if (stateDigests.has(digest)) throw new Error(`${boardState} animation duplicates another required state`);
    stateDigests.add(digest);
    states[boardState] = { sourceState, sourceRow: row, frames, digest };
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
    framesPerState: FRAMES_PER_STATE,
    frameMs: FRAME_MS,
    states: Object.fromEntries(SOURCE_STATES.map(([boardState]) => [boardState, {
      source: states[boardState].sourceState,
      row: states[boardState].sourceRow,
      columns: states[boardState].frames.map(frame => frame.sourceColumn),
      sha256: states[boardState].digest,
    }])),
  };
  fs.writeFileSync(path.join(outputDir, "conversion.json"), JSON.stringify(result, null, 2) + "\n");
  return result;
}

async function selfTest() {
  const sharp = loadSharp();
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "hpet-converter-"));
  try {
    const width = CELL_WIDTH * 8;
    const height = CELL_HEIGHT * 9;
    const pixels = Buffer.alloc(width * height * 4);
    for (let row = 0; row < 5; row++) {
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
    if (result.framesPerState !== 2 || fs.statSync(path.join(output, "preload.bin")).size <= 100) {
      throw new Error("converter did not produce a valid preload");
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
