#!/usr/bin/env node
import fs from 'fs';
import path from 'path';

// MSVC has a 16KB limit per string literal, use 8KB to be safe
const MAX_CHUNK_SIZE = 8000;

if (process.argv.length !== 4) {
  console.error('Usage: node postbuild.js <input.html> <output.h>');
  process.exit(1);
}

const inputPath = path.resolve(process.argv[2]);
const outputPath = path.resolve(process.argv[3]);

if (!fs.existsSync(inputPath)) {
  console.error(`Input file does not exist: ${inputPath}`);
  process.exit(1);
}

// Read HTML
let html = fs.readFileSync(inputPath, 'utf-8');

// Remove whitespace
html = html
  .replace(/\s+/g, ' ')      // collapse consecutive whitespace
  .replace(/>\s+</g, '><')   // remove space between tags
  .trim();

// Escape backslashes and double quotes
html = html.replace(/\\/g, '\\\\').replace(/"/g, '\\"');

// Split into chunks for MSVC compatibility (16KB limit per string literal)
function splitIntoChunks(str, maxSize) {
  const chunks = [];
  for (let i = 0; i < str.length; i += maxSize) {
    chunks.push(str.slice(i, i + maxSize));
  }
  return chunks;
}

const chunks = splitIntoChunks(html, MAX_CHUNK_SIZE);

// Build header content with concatenated string literals
let header = `#pragma once
// Auto-generated from ${path.basename(inputPath)}
// Do not edit manually.
// Split into ${chunks.length} chunks for MSVC compatibility (16KB string literal limit)

constexpr const char INDEX_HTML[] =
`;

// Add each chunk as a separate string literal (C++ concatenates adjacent literals)
for (let i = 0; i < chunks.length; i++) {
  header += `    "${chunks[i]}"`;
  if (i < chunks.length - 1) {
    header += '\n';
  }
}

header += ';\n';

fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, header);

console.log(`Generated header: ${outputPath} (${html.length} bytes in ${chunks.length} chunks)`);

