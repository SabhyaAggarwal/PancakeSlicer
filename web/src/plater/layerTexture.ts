import * as THREE from 'three';
import type { SlicingParams } from './layerProfile';

export function objectLayers(p: SlicingParams, profile: number[]): number[] {
  const out = [0, p.first];
  let printZ = p.first, idx = 0, sliceZ = printZ + 0.5 * p.min;
  while (sliceZ < p.height) {
    let height = p.min;
    if (idx < profile.length) {
      let next = idx + 2;
      while (next < profile.length && sliceZ >= profile[next]) { idx = next; next += 2; }
      const z1 = profile[idx], h1 = profile[idx + 1];
      height = next < profile.length ? h1 + (profile[next + 1] - h1) * (sliceZ - z1) / (profile[next] - z1) : h1;
    }
    sliceZ = printZ + 0.5 * height;
    if (sliceZ >= p.height) break;
    out.push(printZ);
    printZ += height;
    sliceZ = printZ + 0.5 * p.min;
    out.push(printZ);
  }
  return out;
}

const PALETTE = [[0x1a, 0x98, 0x50], [0x66, 0xbd, 0x63], [0xa6, 0xd9, 0x6a], [0xd9, 0xf1, 0xeb], [0xfe, 0xe6, 0xeb], [0xfd, 0xae, 0x61], [0xf4, 0x6d, 0x43], [0xd7, 0x30, 0x27]];
const COLS = 1024;

function zTexture(p: SlicingParams, layers: number[]) {
  const ncells = Math.max(2, Math.min(COLS * 1024, Math.ceil(16 * p.height / p.min)));
  const rows = Math.ceil(ncells / COLS);
  const data = new Uint8Array(rows * COLS * 4);
  const zToCell = (ncells - 1) / p.height, cellToZ = p.height / (ncells - 1);
  let hscale = 2 * Math.max(p.max - p.layer, p.layer - p.min);
  if (hscale === 0) hscale = p.layer;
  for (let i = 0; i + 1 < layers.length; i += 2) {
    const lo = layers[i], h = layers[i + 1] - lo, mid = lo + 0.5 * h, hi = Math.min(layers[i + 1], p.height);
    const idxf = (0.5 * hscale + (h - p.layer)) * (PALETTE.length - 1) / hscale;
    const i1 = Math.min(PALETTE.length - 1, Math.max(0, Math.floor(idxf))), i2 = Math.min(PALETTE.length - 1, i1 + 1), t = idxf - i1;
    const first = Math.min(ncells - 1, Math.max(0, Math.ceil(lo * zToCell))), last = Math.min(ncells - 1, Math.max(0, Math.floor(hi * zToCell)));
    for (let cell = first; cell <= last; cell++) {
      const intensity = Math.cos(Math.PI * 0.7 * (mid - cellToZ * cell) / h);
      const k = 4 * cell;
      for (let c = 0; c < 3; c++) data[k + c] = Math.round(intensity * (PALETTE[i1][c] + (PALETTE[i2][c] - PALETTE[i1][c]) * t));
      data[k + 3] = 255;
    }
  }
  const tex = new THREE.DataTexture(data, COLS, rows, THREE.RGBAFormat);
  tex.needsUpdate = true;
  return { tex, zToCell };
}

const VERTEX =  `
const vec3 TOP = vec3(-0.4574957, 0.4574957, 0.7624929);
const vec3 FRONT = vec3(0.6985074, 0.1397015, 0.6985074);
out vec2 intensity;
out float objectZ;
void main() {
  vec3 n = normalize(normalMatrix * normal);
  vec4 view = modelViewMatrix * vec4(position, 1.0);
  intensity.x = 0.3 + max(dot(n, TOP), 0.0) * 0.48 + max(dot(n, FRONT), 0.0) * 0.18;
  intensity.y = 0.075 * pow(max(dot(-normalize(view.xyz), reflect(-TOP, n)), 0.0), 20.0);
  objectZ = (modelMatrix * vec4(position, 1.0)).z;
  gl_Position = projectionMatrix * view;
}`;
const FRAGMENT =  `
uniform sampler2D zTexture;
uniform float zToCell;
uniform float zCursor;
uniform float zBand;
in vec2 intensity;
in float objectZ;
out vec4 outColor;
void main() {
  float cell = floor(zToCell * objectZ);
  vec4 color = vec4(0.25, 0.25, 0.25, 1.0);
  if (cell >= 0.0) {
    ivec2 size = textureSize(zTexture, 0);
    float row = floor(cell / float(size.x));
    if (row < float(size.y)) color = texelFetch(zTexture, ivec2(int(cell - row * float(size.x)), int(row)), 0);
  }
  float blend = zBand > 0.0 ? 0.25 * cos(min(3.14159265, abs(3.14159265 * (objectZ - zCursor) * 1.8 / zBand))) + 0.25 : 0.0;
  outColor = vec4(vec3(intensity.y), 1.0) + intensity.x * mix(color, vec4(1.0, 1.0, 0.0, 1.0), blend);
}`;

const cache = new Map<string, { material: THREE.ShaderMaterial; tex: THREE.DataTexture }>();

export function layerMaterial(p: SlicingParams, profile: number[], cursor: { z: number; band: number } | null) {
  const key = JSON.stringify([p, profile]);
  let entry = cache.get(key);
  if (!entry) {
    for (const old of cache.values()) { old.tex.dispose(); old.material.dispose(); }
    cache.clear();
    const { tex, zToCell } = zTexture(p, objectLayers(p, profile));
    const material = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3, vertexShader: VERTEX, fragmentShader: FRAGMENT,
      uniforms: { zTexture: { value: tex }, zToCell: { value: zToCell }, zCursor: { value: 0 }, zBand: { value: 0 } },
    });
    entry = { material, tex };
    cache.set(key, entry);
  }
  const u = entry.material.uniforms;
  u.zCursor.value = cursor?.z ?? 0;
  u.zBand.value = cursor?.band ?? 0;
  return entry.material;
}
