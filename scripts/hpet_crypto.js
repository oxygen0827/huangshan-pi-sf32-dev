#!/usr/bin/env node
"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

function usage() {
  throw new Error(
    "usage: hpet_crypto.js generate <private.pem> <public.pem> | " +
    "sign <private.pem> <payload> <signature> | verify <public.pem> <payload> <signature> | --self-test"
  );
}

function ensureParent(filePath) {
  fs.mkdirSync(path.dirname(path.resolve(filePath)), { recursive: true });
}

function generate(privatePath, publicPath) {
  ensureParent(privatePath);
  ensureParent(publicPath);
  const { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
  fs.writeFileSync(privatePath, privateKey.export({ type: "pkcs8", format: "pem" }), { mode: 0o600 });
  fs.chmodSync(privatePath, 0o600);
  fs.writeFileSync(publicPath, publicKey.export({ type: "spki", format: "pem" }), { mode: 0o644 });
}

function sign(privatePath, payloadPath, signaturePath) {
  const key = crypto.createPrivateKey(fs.readFileSync(privatePath));
  const signature = crypto.sign(null, fs.readFileSync(payloadPath), key);
  if (signature.length !== 64) throw new Error(`unexpected Ed25519 signature length: ${signature.length}`);
  ensureParent(signaturePath);
  fs.writeFileSync(signaturePath, signature);
}

function verify(publicPath, payloadPath, signaturePath) {
  const key = crypto.createPublicKey(fs.readFileSync(publicPath));
  const valid = crypto.verify(
    null,
    fs.readFileSync(payloadPath),
    key,
    fs.readFileSync(signaturePath)
  );
  if (!valid) {
    process.stderr.write("invalid Ed25519 signature\n");
    process.exitCode = 2;
  }
}

function selfTest() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "hpet-crypto-"));
  try {
    const privatePath = path.join(root, "private.pem");
    const publicPath = path.join(root, "public.pem");
    const payloadPath = path.join(root, "payload.bin");
    const signaturePath = path.join(root, "signature.bin");
    generate(privatePath, publicPath);
    fs.writeFileSync(payloadPath, "signed pet\n");
    sign(privatePath, payloadPath, signaturePath);
    verify(publicPath, payloadPath, signaturePath);
    fs.appendFileSync(payloadPath, "tampered");
    const key = crypto.createPublicKey(fs.readFileSync(publicPath));
    if (crypto.verify(null, fs.readFileSync(payloadPath), key, fs.readFileSync(signaturePath))) {
      throw new Error("tampered payload passed Ed25519 verification");
    }
    process.stdout.write("hpet_crypto self-test ok\n");
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === "--self-test") return selfTest();
  if (args[0] === "generate" && args.length === 3) return generate(args[1], args[2]);
  if (args[0] === "sign" && args.length === 4) return sign(args[1], args[2], args[3]);
  if (args[0] === "verify" && args.length === 4) return verify(args[1], args[2], args[3]);
  return usage();
}

try {
  main();
} catch (error) {
  process.stderr.write(`hpet_crypto: ${error.message}\n`);
  process.exitCode = 1;
}
