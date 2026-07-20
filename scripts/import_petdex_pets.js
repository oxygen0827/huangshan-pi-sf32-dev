#!/usr/bin/env node
"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const zlib = require("node:zlib");

const ROOT = path.resolve(__dirname, "..");
const CONFIG_PATH = path.join(ROOT, "scripts/petdex_pets.json");
const OUTPUT_ROOT = path.join(ROOT, "scripts/runtime_apps/codex_pet/assets/pets");
const BUNDLED_SHARP_PATH = "/Applications/ChatGPT.app/Contents/Resources/cua_node/lib/node_modules/sharp";
const CELL_WIDTH = 192;
const CELL_HEIGHT = 208;
const OUTPUT_WIDTH = 160;
const OUTPUT_HEIGHT = 173;
const RLE_MAGIC = 0x454c5256; // VRLE
const RLE_VERSION = 1;
const RLE_HEADER_SIZE = 20;
const LV_IMG_CF_TRUE_COLOR_ALPHA = 5;
const STATE_PACK_MAGIC = 0x54535056; // VPST
const STATE_PACK_VERSION = 2;
const LEGACY_STATE_PACK_VERSION = 1;
const STATE_PACK_HEADER_SIZE = 16;
const PRELOAD_PACK_MAGIC = 0x43504256; // VBPC
const PRELOAD_PACK_VERSION = 1;
const PRELOAD_PACK_HEADER_SIZE = 16;
const PRELOAD_STATES = ["idle", "running"];
const PRELOAD_FRAMES_PER_STATE = 2;
const PRELOAD_PATH = path.join(OUTPUT_ROOT, "preload.bin");
const FRAME_MS = 180;
const CHECK_ONLY = process.argv.includes("--check");
const CHECK_CONFIG_ONLY = process.argv.includes("--check-config");
const UPGRADE_RAW = process.argv.includes("--upgrade-raw");
const BUILD_PRELOAD = process.argv.includes("--build-preload");
const STATE_ROWS = [
  ["idle", 0],
  ["ready", 3],
  ["blocked", 5],
  ["needs", 6],
  ["running", 7],
];

function loadConfig() {
  const value = JSON.parse(fs.readFileSync(CONFIG_PATH, "utf8"));
  if (value.schemaVersion !== 1 || !Array.isArray(value.pets) || value.pets.length === 0) {
    throw new Error("petdex_pets.json must contain a non-empty schemaVersion 1 pet list");
  }
  const seen = new Set();
  for (const pet of value.pets) {
    if (!pet || !/^[a-z0-9][a-z0-9-]{0,23}$/.test(pet.slug || "") ||
        !pet.name || !pet.author || !pet.petJsonUrl || !pet.spritesheetUrl || !pet.sourceUrl) {
      throw new Error("petdex_pets.json contains an invalid pet entry");
    }
    if (seen.has(pet.slug)) throw new Error(`Duplicate Petdex slug: ${pet.slug}`);
    seen.add(pet.slug);
  }
  return value.pets;
}

function loadSharp() {
  const candidates = [process.env.CODEX_PET_SHARP, "sharp", BUNDLED_SHARP_PATH]
    .filter((candidate, index, values) => candidate && values.indexOf(candidate) === index);
  const failures = [];
  for (const candidate of candidates) {
    try {
      return require(candidate);
    } catch (error) {
      failures.push(`${candidate}: ${error.code || error.message}`);
    }
  }
  throw new Error(
    "No Sharp image converter found; install 'sharp' or set CODEX_PET_SHARP. " +
    `Tried: ${failures.join("; ")}`
  );
}

function encodeRleFrame(data, info) {
  if (info.width !== OUTPUT_WIDTH || info.height !== OUTPUT_HEIGHT || info.channels !== 4) {
    throw new Error(`Unexpected raw frame: ${info.width}x${info.height}x${info.channels}`);
  }
  const records = [];
  let previous = -1;
  let count = 0;
  const flush = () => {
    if (!count) return;
    const record = Buffer.allocUnsafe(5);
    record.writeUInt16LE(count, 0);
    record.writeUInt16LE(previous & 0xffff, 2);
    record[4] = (previous >>> 16) & 0xff;
    records.push(record);
  };
  for (let offset = 0; offset < data.length; offset += 4) {
    const rgb565 = ((data[offset] & 0xf8) << 8) |
      ((data[offset + 1] & 0xfc) << 3) |
      (data[offset + 2] >> 3);
    const pixel = rgb565 | (data[offset + 3] << 16);
    if (pixel !== previous || count === 0xffff) {
      flush();
      previous = pixel;
      count = 1;
    } else {
      count += 1;
    }
  }
  flush();
  const header = Buffer.alloc(RLE_HEADER_SIZE);
  header.writeUInt32LE(RLE_MAGIC, 0);
  header.writeUInt16LE(RLE_VERSION, 4);
  header.writeUInt16LE(OUTPUT_WIDTH, 6);
  header.writeUInt16LE(OUTPUT_HEIGHT, 8);
  header.writeUInt16LE(LV_IMG_CF_TRUE_COLOR_ALPHA, 10);
  header.writeUInt32LE(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3, 12);
  header.writeUInt32LE(records.length, 16);
  return Buffer.concat([header, ...records]);
}

function encodeRawFrame(data, info) {
  if (info.width !== OUTPUT_WIDTH || info.height !== OUTPUT_HEIGHT || info.channels !== 4) {
    throw new Error(`Unexpected raw frame: ${info.width}x${info.height}x${info.channels}`);
  }
  const frame = Buffer.allocUnsafe(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3);
  for (let source = 0, target = 0; source < data.length; source += 4, target += 3) {
    const rgb565 = ((data[source] & 0xf8) << 8) |
      ((data[source + 1] & 0xfc) << 3) |
      (data[source + 2] >> 3);
    frame.writeUInt16LE(rgb565, target);
    frame[target + 2] = data[source + 3];
  }
  return frame;
}

function verifyRleData(data, label) {
  if (data.length < RLE_HEADER_SIZE || data.readUInt32LE(0) !== RLE_MAGIC ||
      data.readUInt16LE(4) !== RLE_VERSION || data.readUInt16LE(6) !== OUTPUT_WIDTH ||
      data.readUInt16LE(8) !== OUTPUT_HEIGHT || data.readUInt16LE(10) !== LV_IMG_CF_TRUE_COLOR_ALPHA ||
      data.readUInt32LE(12) !== OUTPUT_WIDTH * OUTPUT_HEIGHT * 3) {
    throw new Error(`Invalid VRLE header: ${label}`);
  }
  const runCount = data.readUInt32LE(16);
  if (!runCount || data.length !== RLE_HEADER_SIZE + runCount * 5) {
    throw new Error(`Invalid VRLE length: ${label}`);
  }
  let pixels = 0;
  for (let offset = RLE_HEADER_SIZE; offset < data.length; offset += 5) {
    const count = data.readUInt16LE(offset);
    if (!count) throw new Error(`Empty VRLE run: ${label}`);
    pixels += count;
  }
  if (pixels !== OUTPUT_WIDTH * OUTPUT_HEIGHT) throw new Error(`Invalid VRLE pixels: ${label}`);
}

function decodeRleFrame(data, label) {
  verifyRleData(data, label);
  const frame = Buffer.allocUnsafe(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3);
  let target = 0;
  for (let offset = RLE_HEADER_SIZE; offset < data.length; offset += 5) {
    const count = data.readUInt16LE(offset);
    for (let pixel = 0; pixel < count; pixel++) {
      frame[target++] = data[offset + 2];
      frame[target++] = data[offset + 3];
      frame[target++] = data[offset + 4];
    }
  }
  return frame;
}

function verifyRawData(data, label) {
  if (data.length !== OUTPUT_WIDTH * OUTPUT_HEIGHT * 3) {
    throw new Error(`Invalid raw frame length: ${label}`);
  }
}

function encodeStatePack(frames) {
  const header = Buffer.alloc(STATE_PACK_HEADER_SIZE + frames.length * 8);
  header.writeUInt32LE(STATE_PACK_MAGIC, 0);
  header.writeUInt16LE(STATE_PACK_VERSION, 4);
  header.writeUInt16LE(frames.length, 6);
  header.writeUInt16LE(OUTPUT_WIDTH, 8);
  header.writeUInt16LE(OUTPUT_HEIGHT, 10);
  header.writeUInt32LE(frames.length * 8, 12);
  let offset = header.length;
  frames.forEach((frame, index) => {
    header.writeUInt32LE(offset, STATE_PACK_HEADER_SIZE + index * 8);
    header.writeUInt32LE(frame.length, STATE_PACK_HEADER_SIZE + index * 8 + 4);
    offset += frame.length;
  });
  return Buffer.concat([header, ...frames]);
}

function verifyStatePack(filePath, expectedCount) {
  const data = fs.readFileSync(filePath);
  if (data.length < STATE_PACK_HEADER_SIZE || data.readUInt32LE(0) !== STATE_PACK_MAGIC ||
      data.readUInt16LE(4) !== STATE_PACK_VERSION || data.readUInt16LE(6) !== expectedCount ||
      data.readUInt16LE(8) !== OUTPUT_WIDTH || data.readUInt16LE(10) !== OUTPUT_HEIGHT ||
      data.readUInt32LE(12) !== expectedCount * 8) {
    throw new Error(`Invalid state pack header: ${filePath}`);
  }
  let expectedOffset = STATE_PACK_HEADER_SIZE + expectedCount * 8;
  for (let frame = 0; frame < expectedCount; frame++) {
    const offset = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8);
    const length = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8 + 4);
    if (offset !== expectedOffset || offset + length > data.length) {
      throw new Error(`Invalid state pack index: ${filePath}`);
    }
    verifyRawData(data.subarray(offset, offset + length), `${filePath}#${frame}`);
    expectedOffset += length;
  }
  if (expectedOffset !== data.length) throw new Error(`Trailing state pack data: ${filePath}`);
}

function rawStateFrames(pet, state, limit) {
  const petDir = path.join(OUTPUT_ROOT, pet.slug);
  const meta = parseMeta(petDir);
  const expectedCount = Number(meta[state]);
  const data = fs.readFileSync(path.join(petDir, `${state}.bin`));
  if (expectedCount < limit || data.readUInt16LE(6) !== expectedCount) {
    throw new Error(`${pet.slug} needs at least ${limit} ${state} frames`);
  }
  const frames = [];
  for (let frame = 0; frame < limit; frame++) {
    const offset = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8);
    const length = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8 + 4);
    if (offset + length > data.length) throw new Error(`Invalid ${state} frame index for ${pet.slug}`);
    const raw = data.subarray(offset, offset + length);
    verifyRawData(raw, `${pet.slug}/${state}#${frame}`);
    frames.push(raw);
  }
  return frames;
}

function buildPreloadPack(pets) {
  const compressed = [];
  for (const pet of pets) {
    for (const state of PRELOAD_STATES) {
      for (const raw of rawStateFrames(pet, state, PRELOAD_FRAMES_PER_STATE)) {
        compressed.push(zlib.deflateSync(raw, { level: 9 }));
      }
    }
  }
  const indexBytes = compressed.length * 8;
  const header = Buffer.alloc(PRELOAD_PACK_HEADER_SIZE + indexBytes);
  header.writeUInt32LE(PRELOAD_PACK_MAGIC, 0);
  header.writeUInt16LE(PRELOAD_PACK_VERSION, 4);
  header.writeUInt16LE(pets.length, 6);
  header.writeUInt16LE(OUTPUT_WIDTH, 8);
  header.writeUInt16LE(OUTPUT_HEIGHT, 10);
  header.writeUInt16LE(PRELOAD_STATES.length, 12);
  header.writeUInt16LE(PRELOAD_FRAMES_PER_STATE, 14);
  let offset = header.length;
  compressed.forEach((frame, index) => {
    header.writeUInt32LE(offset, PRELOAD_PACK_HEADER_SIZE + index * 8);
    header.writeUInt32LE(frame.length, PRELOAD_PACK_HEADER_SIZE + index * 8 + 4);
    offset += frame.length;
  });
  fs.writeFileSync(PRELOAD_PATH, Buffer.concat([header, ...compressed]));
}

function verifyPreloadPack(pets) {
  const data = fs.readFileSync(PRELOAD_PATH);
  const entries = pets.length * PRELOAD_STATES.length * PRELOAD_FRAMES_PER_STATE;
  if (data.length < PRELOAD_PACK_HEADER_SIZE + entries * 8 ||
      data.readUInt32LE(0) !== PRELOAD_PACK_MAGIC ||
      data.readUInt16LE(4) !== PRELOAD_PACK_VERSION ||
      data.readUInt16LE(6) !== pets.length ||
      data.readUInt16LE(8) !== OUTPUT_WIDTH || data.readUInt16LE(10) !== OUTPUT_HEIGHT ||
      data.readUInt16LE(12) !== PRELOAD_STATES.length ||
      data.readUInt16LE(14) !== PRELOAD_FRAMES_PER_STATE) {
    throw new Error(`Invalid preload pack header: ${PRELOAD_PATH}`);
  }
  let entry = 0;
  for (const pet of pets) {
    for (const state of PRELOAD_STATES) {
      const expectedFrames = rawStateFrames(pet, state, PRELOAD_FRAMES_PER_STATE);
      for (const expected of expectedFrames) {
        const offset = data.readUInt32LE(PRELOAD_PACK_HEADER_SIZE + entry * 8);
        const length = data.readUInt32LE(PRELOAD_PACK_HEADER_SIZE + entry * 8 + 4);
        if (offset < PRELOAD_PACK_HEADER_SIZE + entries * 8 || offset + length > data.length) {
          throw new Error(`Invalid preload pack index: ${PRELOAD_PATH}#${entry}`);
        }
        const actual = zlib.inflateSync(data.subarray(offset, offset + length));
        if (!actual.equals(expected)) throw new Error(`Preload frame mismatch: ${PRELOAD_PATH}#${entry}`);
        entry += 1;
      }
    }
  }
}

function parseMeta(petDir) {
  const lines = fs.readFileSync(path.join(petDir, "pet.txt"), "utf8").trim().split("\n");
  if (lines.shift() !== "VBPET1") throw new Error(`Invalid pet.txt in ${petDir}`);
  const values = Object.fromEntries(lines.map((line) => {
    const index = line.indexOf("=");
    if (index <= 0) throw new Error(`Invalid pet.txt line in ${petDir}`);
    return [line.slice(0, index), line.slice(index + 1)];
  }));
  return values;
}

function upgradeLegacyPet(pet) {
  const petDir = path.join(OUTPUT_ROOT, pet.slug);
  const meta = parseMeta(petDir);
  for (const [state] of STATE_ROWS) {
    const expectedCount = Number(meta[state]);
    const filePath = path.join(petDir, `${state}.bin`);
    const data = fs.readFileSync(filePath);
    if (data.readUInt32LE(0) !== STATE_PACK_MAGIC ||
        data.readUInt16LE(4) !== LEGACY_STATE_PACK_VERSION ||
        data.readUInt16LE(6) !== expectedCount) {
      throw new Error(`Not a legacy VPST v1 pack: ${filePath}`);
    }
    const frames = [];
    for (let frame = 0; frame < expectedCount; frame++) {
      const offset = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8);
      const length = data.readUInt32LE(STATE_PACK_HEADER_SIZE + frame * 8 + 4);
      if (offset + length > data.length) throw new Error(`Invalid legacy state pack index: ${filePath}`);
      frames.push(decodeRleFrame(data.subarray(offset, offset + length), `${filePath}#${frame}`));
    }
    fs.writeFileSync(filePath, encodeStatePack(frames));
  }
  const sourcePath = path.join(petDir, "source.json");
  const source = JSON.parse(fs.readFileSync(sourcePath, "utf8"));
  source.outputFormat = "VPST v2 (raw RGB565 + alpha frames)";
  fs.writeFileSync(sourcePath, JSON.stringify(source, null, 2) + "\n", "utf8");
  verifyPet(pet);
}

function verifyPet(pet) {
  const petDir = path.join(OUTPUT_ROOT, pet.slug);
  const meta = parseMeta(petDir);
  if (meta.slug !== pet.slug || meta.name !== pet.name || meta.author !== pet.author ||
      Number(meta.width) !== OUTPUT_WIDTH || Number(meta.height) !== OUTPUT_HEIGHT ||
      Number(meta.frame_ms) < 60 || Number(meta.frame_ms) > 2000) {
    throw new Error(`Metadata mismatch for ${pet.slug}`);
  }
  for (const [state] of STATE_ROWS) {
    const count = Number(meta[state]);
    if (!Number.isInteger(count) || count < 1 || count > 8) {
      throw new Error(`Invalid ${state} frame count for ${pet.slug}`);
    }
    verifyStatePack(path.join(petDir, `${state}.bin`), count);
  }
}

async function download(url) {
  const response = await fetch(url, { redirect: "follow" });
  if (!response.ok) throw new Error(`Download failed (${response.status}): ${url}`);
  return Buffer.from(await response.arrayBuffer());
}

async function importPet(sharp, pet) {
  const [petJsonBytes, spritesheet] = await Promise.all([
    download(pet.petJsonUrl),
    download(pet.spritesheetUrl),
  ]);
  const metadata = await sharp(spritesheet).metadata();
  if (!metadata.width || !metadata.height || metadata.width % CELL_WIDTH !== 0 ||
      metadata.height % CELL_HEIGHT !== 0 || metadata.width / CELL_WIDTH !== 8) {
    throw new Error(`Unexpected spritesheet dimensions for ${pet.slug}: ${metadata.width}x${metadata.height}`);
  }
  const rows = metadata.height / CELL_HEIGHT;
  if (rows !== 9 && rows !== 11) {
    throw new Error(`Unsupported spritesheet row count for ${pet.slug}: ${rows}`);
  }
  const sourceMeta = JSON.parse(petJsonBytes.toString("utf8"));
  if (sourceMeta.id !== pet.slug && sourceMeta.id !== pet.name.toLowerCase()) {
    throw new Error(`Pet metadata id mismatch for ${pet.slug}: ${sourceMeta.id}`);
  }

  const petDir = path.join(OUTPUT_ROOT, pet.slug);
  fs.rmSync(petDir, { recursive: true, force: true });
  fs.mkdirSync(petDir, { recursive: true });
  const counts = {};
  for (const [state, row] of STATE_ROWS) {
    const frames = [];
    for (let column = 0; column < 8; column++) {
      const extracted = await sharp(spritesheet)
        .extract({ left: column * CELL_WIDTH, top: row * CELL_HEIGHT,
          width: CELL_WIDTH, height: CELL_HEIGHT })
        .ensureAlpha()
        .raw()
        .toBuffer({ resolveWithObject: true });
      let visible = 0;
      for (let offset = 3; offset < extracted.data.length; offset += 4) {
        if (extracted.data[offset] > 8) visible++;
      }
      if (visible === 0) break;
      const resized = await sharp(extracted.data, {
        raw: { width: CELL_WIDTH, height: CELL_HEIGHT, channels: 4 },
      }).resize(OUTPUT_WIDTH, OUTPUT_HEIGHT, { kernel: "nearest" })
        .raw()
        .toBuffer({ resolveWithObject: true });
      frames.push(encodeRawFrame(resized.data, resized.info));
    }
    if (frames.length === 0) throw new Error(`${pet.slug} has no frames for ${state}`);
    counts[state] = frames.length;
    fs.writeFileSync(path.join(petDir, `${state}.bin`), encodeStatePack(frames));
  }
  const metaLines = [
    "VBPET1",
    `slug=${pet.slug}`,
    `name=${pet.name}`,
    `author=${pet.author}`,
    `width=${OUTPUT_WIDTH}`,
    `height=${OUTPUT_HEIGHT}`,
    `frame_ms=${FRAME_MS}`,
    ...STATE_ROWS.map(([state]) => `${state}=${counts[state]}`),
  ];
  fs.writeFileSync(path.join(petDir, "pet.txt"), metaLines.join("\n") + "\n", "utf8");
  fs.writeFileSync(path.join(petDir, "source.json"), JSON.stringify({
    source: "Petdex user-installed asset; rights remain with the submitting author",
    sourceUrl: pet.sourceUrl,
    author: pet.author,
    petJsonUrl: pet.petJsonUrl,
    spritesheetUrl: pet.spritesheetUrl,
    sourceSha256: crypto.createHash("sha256").update(spritesheet).digest("hex"),
    sourceDimensions: [metadata.width, metadata.height],
    cellDimensions: [CELL_WIDTH, CELL_HEIGHT],
    outputDimensions: [OUTPUT_WIDTH, OUTPUT_HEIGHT],
    outputFormat: "VPST v2 (raw RGB565 + alpha frames)",
    states: counts,
  }, null, 2) + "\n", "utf8");
  verifyPet(pet);
  return counts;
}

async function main() {
  const pets = loadConfig();
  if (CHECK_CONFIG_ONLY) {
    console.log(`Validated ${pets.length} Petdex import entries in ${CONFIG_PATH}`);
    return;
  }
  if (UPGRADE_RAW) {
    for (const pet of pets) {
      upgradeLegacyPet(pet);
      console.log(`Upgraded ${pet.slug} to VPST v2 raw frames`);
    }
    buildPreloadPack(pets);
    verifyPreloadPack(pets);
    return;
  }
  if (BUILD_PRELOAD) {
    for (const pet of pets) verifyPet(pet);
    buildPreloadPack(pets);
    verifyPreloadPack(pets);
    console.log(`Generated ${PRELOAD_PATH}`);
    return;
  }
  if (CHECK_ONLY) {
    const catalog = fs.readFileSync(path.join(OUTPUT_ROOT, "catalog.txt"), "utf8");
    if (catalog.split("\n")[0] !== "VBPETS1") throw new Error("Invalid pet catalog header");
    for (const pet of pets) {
      verifyPet(pet);
      if (!catalog.includes(`\n${pet.slug}|${pet.name}|${pet.author}\n`)) {
        throw new Error(`Pet catalog is missing ${pet.slug}`);
      }
    }
    verifyPreloadPack(pets);
    console.log(`Verified ${pets.length} Petdex pets in ${OUTPUT_ROOT}`);
    return;
  }
  const sharp = loadSharp();
  fs.mkdirSync(OUTPUT_ROOT, { recursive: true });
  const catalog = ["VBPETS1"];
  for (const pet of pets) {
    const counts = await importPet(sharp, pet);
    catalog.push(`${pet.slug}|${pet.name}|${pet.author}`);
    console.log(`Imported ${pet.slug}: ${STATE_ROWS.map(([state]) => `${state}=${counts[state]}`).join(" ")}`);
  }
  fs.writeFileSync(path.join(OUTPUT_ROOT, "catalog.txt"), catalog.join("\n") + "\n", "utf8");
  buildPreloadPack(pets);
  verifyPreloadPack(pets);
  console.log(`Generated ${pets.length} board-ready Petdex pets in ${OUTPUT_ROOT}`);
}

main().catch((error) => {
  console.error(`import_petdex_pets: ${error.message}`);
  process.exitCode = 1;
});
