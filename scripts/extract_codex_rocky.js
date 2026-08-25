#!/usr/bin/env node
"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

const ROOT = path.resolve(__dirname, "..");
const APP_ROOT = "/Applications/ChatGPT.app/Contents/Resources";
const ASAR_PATH = path.join(APP_ROOT, "app.asar");
const SHARP_PATH = path.join(APP_ROOT, "cua_node/lib/node_modules/sharp");
const OUTPUT_DIR = path.join(ROOT, "scripts/runtime_apps/codex_pet/assets/rocky");
const EXPECTED_COLUMNS = 8;
const EXPECTED_ROWS = 11;
const CHECK_ONLY = process.argv.includes("--check");
const ROCKY_RLE_MAGIC = 0x454c5256; // VRLE, little-endian on disk
const ROCKY_RLE_VERSION = 1;
const LV_IMG_CF_TRUE_COLOR_ALPHA = 5;
const OUTPUT_WIDTH = 160;
const OUTPUT_HEIGHT = 173;
const RLE_HEADER_SIZE = 20;

const FRAMES = [
  ["idle0.rle", 0, 0],
  ["idle1.rle", 0, 1],
  ["ready0.rle", 3, 0],
  ["ready1.rle", 3, 1],
  ["blocked0.rle", 5, 1],
  ["blocked1.rle", 5, 2],
  ["needs0.rle", 6, 0],
  ["needs1.rle", 6, 1],
  ["running0.rle", 7, 0],
  ["running1.rle", 7, 1],
];

function encodeRleFrame(data, info) {
  if (info.width !== OUTPUT_WIDTH || info.height !== OUTPUT_HEIGHT || info.channels !== 4) {
    throw new Error(`Unexpected Rocky raw frame: ${info.width}x${info.height}x${info.channels}`);
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
  header.writeUInt32LE(ROCKY_RLE_MAGIC, 0);
  header.writeUInt16LE(ROCKY_RLE_VERSION, 4);
  header.writeUInt16LE(OUTPUT_WIDTH, 6);
  header.writeUInt16LE(OUTPUT_HEIGHT, 8);
  header.writeUInt16LE(LV_IMG_CF_TRUE_COLOR_ALPHA, 10);
  header.writeUInt32LE(OUTPUT_WIDTH * OUTPUT_HEIGHT * 3, 12);
  header.writeUInt32LE(records.length, 16);
  return Buffer.concat([header, ...records]);
}

function verifyRleFrame(outputPath) {
  const data = fs.readFileSync(outputPath);
  if (data.length < RLE_HEADER_SIZE || data.readUInt32LE(0) !== ROCKY_RLE_MAGIC ||
      data.readUInt16LE(4) !== ROCKY_RLE_VERSION || data.readUInt16LE(6) !== OUTPUT_WIDTH ||
      data.readUInt16LE(8) !== OUTPUT_HEIGHT || data.readUInt16LE(10) !== LV_IMG_CF_TRUE_COLOR_ALPHA ||
      data.readUInt32LE(12) !== OUTPUT_WIDTH * OUTPUT_HEIGHT * 3) {
    throw new Error(`Rocky RLE header is invalid: ${path.basename(outputPath)}`);
  }
  const runCount = data.readUInt32LE(16);
  if (data.length !== RLE_HEADER_SIZE + runCount * 5) {
    throw new Error(`Rocky RLE length is invalid: ${path.basename(outputPath)}`);
  }
  let pixels = 0;
  for (let offset = RLE_HEADER_SIZE; offset < data.length; offset += 5) {
    const count = data.readUInt16LE(offset);
    if (!count) throw new Error(`Rocky RLE contains an empty run: ${path.basename(outputPath)}`);
    pixels += count;
  }
  if (pixels !== OUTPUT_WIDTH * OUTPUT_HEIGHT) {
    throw new Error(`Rocky RLE pixel count is invalid: ${path.basename(outputPath)}`);
  }
}

function readAsarIndex(filePath) {
  const descriptor = fs.openSync(filePath, "r");
  try {
    const prefix = Buffer.alloc(16);
    fs.readSync(descriptor, prefix, 0, prefix.length, 0);
    const headerSize = prefix.readUInt32LE(4);
    const jsonLength = prefix.readUInt32LE(12);
    const header = Buffer.alloc(jsonLength);
    fs.readSync(descriptor, header, 0, jsonLength, 16);
    return {
      contentOffset: 8 + headerSize,
      root: JSON.parse(header.toString("utf8")),
    };
  } finally {
    fs.closeSync(descriptor);
  }
}

function findFile(node, matcher, prefix = "") {
  for (const [name, entry] of Object.entries(node.files || {})) {
    const current = prefix ? `${prefix}/${name}` : name;
    if (entry.files) {
      const found = findFile(entry, matcher, current);
      if (found) return found;
    } else if (matcher(current)) {
      return { ...entry, path: current };
    }
  }
  return null;
}

async function main() {
  if (!fs.existsSync(ASAR_PATH) && CHECK_ONLY) {
    console.log("Codex app is not installed; Rocky extraction is optional and vector fallback remains active");
    return;
  }
  if (!fs.existsSync(ASAR_PATH)) throw new Error(`Codex app resource not found: ${ASAR_PATH}`);
  const { contentOffset, root } = readAsarIndex(ASAR_PATH);
  const entry = findFile(
    root,
    (value) => /^webview\/assets\/rocky-spritesheet-v5-[A-Za-z0-9_-]+\.webp$/.test(value),
  );
  if (!entry) throw new Error("Rocky v5 spritesheet was not found in the installed Codex app");
  const descriptor = fs.openSync(ASAR_PATH, "r");
  let spritesheet;
  try {
    spritesheet = Buffer.alloc(Number(entry.size));
    fs.readSync(descriptor, spritesheet, 0, spritesheet.length, contentOffset + Number(entry.offset));
  } finally {
    fs.closeSync(descriptor);
  }
  const digest = crypto.createHash("sha256").update(spritesheet).digest("hex");
  if (digest !== entry.integrity?.hash) throw new Error("Rocky spritesheet integrity check failed");

  const sharp = require(SHARP_PATH);
  const metadata = await sharp(spritesheet).metadata();
  if (!metadata.width || !metadata.height || metadata.width % EXPECTED_COLUMNS !== 0 ||
      metadata.height % EXPECTED_ROWS !== 0) {
    throw new Error(`Unexpected Rocky spritesheet dimensions: ${metadata.width}x${metadata.height}`);
  }
  const cellWidth = metadata.width / EXPECTED_COLUMNS;
  const cellHeight = metadata.height / EXPECTED_ROWS;
  if (CHECK_ONLY) {
    for (const [name] of FRAMES) {
      const outputPath = path.join(OUTPUT_DIR, name);
      if (!fs.existsSync(outputPath)) throw new Error(`Rocky frame is missing: ${name}`);
      verifyRleFrame(outputPath);
    }
    if (fs.readdirSync(OUTPUT_DIR).some((name) => name.endsWith(".png")))
      throw new Error("Stale Rocky PNG frames would reintroduce runtime decoding and package bloat");
    console.log(`Verified ${FRAMES.length} Rocky frames from ${entry.path}`);
    return;
  }
  fs.mkdirSync(OUTPUT_DIR, { recursive: true });
  for (const name of fs.readdirSync(OUTPUT_DIR)) {
    if (name.endsWith(".png") || name.endsWith(".bin") || name.endsWith(".rle"))
      fs.unlinkSync(path.join(OUTPUT_DIR, name));
  }
  for (const [name, row, column] of FRAMES) {
    const frame = await sharp(spritesheet)
      .extract({ left: column * cellWidth, top: row * cellHeight, width: cellWidth, height: cellHeight })
      .resize(OUTPUT_WIDTH, OUTPUT_HEIGHT, { kernel: "nearest" })
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });
    fs.writeFileSync(path.join(OUTPUT_DIR, name), encodeRleFrame(frame.data, frame.info));
  }
  fs.writeFileSync(
    path.join(OUTPUT_DIR, "source.json"),
    JSON.stringify({
      source: "Installed Codex desktop app (local personal-use extraction)",
      asset: entry.path,
      sha256: digest,
      sourceDimensions: [metadata.width, metadata.height],
      cellDimensions: [cellWidth, cellHeight],
      outputDimensions: [OUTPUT_WIDTH, OUTPUT_HEIGHT],
      outputFormat: "VRLE v1 (RGB565 + alpha runs)",
    }, null, 2) + "\n",
    "utf8",
  );
  console.log(`Extracted ${FRAMES.length} verified Rocky frames to ${OUTPUT_DIR}`);
}

main().catch((error) => {
  console.error(`extract_codex_rocky: ${error.message}`);
  process.exitCode = 1;
});
