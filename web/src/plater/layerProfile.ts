



import type * as THREE from 'three';
import { T3 } from '../three-runtime';

const EPSILON = 1e-4;

const LAYER_HEIGHT_CHANGE_STEP = 0.04;

export interface SlicingParams { first: number; layer: number; min: number; max: number; height: number }


export function slicingParams(layer: number, first: number, minLayer: number, maxLayer: number, nozzle: number, height: number): SlicingParams {
  const min = Math.min(minLayer === 0 ? 0.07 : Math.max(0.01, minLayer), layer);
  const max = Math.max(Math.max(min, maxLayer === 0 ? 0.75 * nozzle : maxLayer), layer);
  return { first, layer, min, max, height };
}


export const baseProfile = (p: SlicingParams) => [0, p.first, p.first, p.first, p.height, p.layer];



interface Face { lo: number; hi: number; cos: number; sin: number }


function faces(positions: ArrayLike<number>, index: ArrayLike<number> | null, matrix: THREE.Matrix4): Face[] {
  const out: Face[] = [];
  const v = [new T3.Vector3(), new T3.Vector3(), new T3.Vector3()];
  const n = (index ? index.length : positions.length / 3) / 3;
  let zmin = Infinity;
  for (let t = 0; t < n; t++) {
    for (let k = 0; k < 3; k++) {
      const i = index ? index[3 * t + k] : 3 * t + k;
      v[k].set(positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]).applyMatrix4(matrix);
    }
    const nrm = v[2].clone().sub(v[1]).cross(v[0].clone().sub(v[1])).normalize();
    const lo = Math.min(v[0].z, v[1].z, v[2].z);
    zmin = Math.min(zmin, lo);
    out.push({ lo, hi: Math.max(v[0].z, v[1].z, v[2].z), cos: Math.abs(nrm.z), sin: Math.hypot(nrm.x, nrm.y) });
  }
  for (const f of out) f.lo -= zmin, f.hi -= zmin;
  return out.sort((a, b) => a.lo - b.lo || a.hi - b.hi);
}

const slopeHeight = (f: Face, dev: number) => Math.min(dev / 0.184, f.cos > 1e-5 ? 1.44 * dev * Math.sqrt(f.sin / f.cos) : Infinity);


export function adaptiveProfile(p: SlicingParams, positions: ArrayLike<number>, index: ArrayLike<number> | null, matrix: THREE.Matrix4, quality: number): number[] {
  const fs = faces(positions, index, matrix);
  const lerp = (a: number, b: number, t: number) => a + (b - a) * t;
  const dev = quality < 0.5 ? lerp(p.min, p.layer, 2 * quality) : lerp(p.max, p.layer, 2 * (1 - quality));
  let current = 0;
  const next = (z: number) => {
    let height = p.max, id = current, first = false;
    for (; id < fs.length; id++) {
      const f = fs[id];
      if (f.lo >= z) break;
      if (f.hi > z) {
        if (!first) { first = true; current = id; }
        if (f.hi < z + EPSILON) continue;
        height = Math.min(height, slopeHeight(f, dev));
      }
    }
    height = Math.max(height, p.min);
    if (height > p.min) {
      for (; id < fs.length; id++) {
        const f = fs[id];
        if (f.lo >= z + height) break;
        if (f.hi < z + EPSILON) continue;
        const reduced = slopeHeight(f, dev), dz = f.lo - z;
        if (reduced < dz) height = dz;
        else if (reduced < height) height = reduced;
      }
      height = Math.max(height, p.min);
    }
    return height;
  };
  const out = [0, p.first, p.first, p.first];
  let z = p.first;
  while (z + EPSILON < p.height) {
    let h = Math.min(next(z), p.max);
    const last = out[out.length - 1];
    if (h > last + LAYER_HEIGHT_CHANGE_STEP) h = last + LAYER_HEIGHT_CHANGE_STEP;
    else if (h < last - LAYER_HEIGHT_CHANGE_STEP) h = last - LAYER_HEIGHT_CHANGE_STEP;
    out.push(z, h);
    z += h;
  }
  const gap = p.height - out[out.length - 2];
  if (gap > 0) out.push(p.height, Math.min(p.max, Math.max(p.min, gap)));
  return out;
}




export function smoothProfile(profile: number[], p: SlicingParams, radius: number, keepMin: boolean): number[] {
  const r = Math.max(1, Math.round(radius));
  const sigma = 0.3 * (r - 1) + 0.8, two = 2 * sigma * sigma, norm = 1 / Math.sqrt(Math.PI * two);
  const kernel = Array.from({ length: 2 * r + 1 }, (_, i) => norm * Math.exp(-((i - r) ** 2) / two));
  const skip = 4;
  const blur = (prof: number[]) => {
    if (prof.length - skip < 6) return prof;
    const out = prof.slice(0, skip);
    const dh = p.max - p.min, inv = dh !== 0 ? 1 / dh : 1, band = r * p.layer;
    for (let i = skip; i < prof.length; i += 2) {
      const zi = prof[i], hi = prof[i + 1];
      let height = 0, total = 0;
      for (let j = Math.max(i - 2 * r, skip); j <= Math.min(i + 2 * r, prof.length - 2); j += 2) {
        const dz = Math.abs(zi - prof[j]);
        if (dz * p.layer > band) continue;
        const w = kernel[r + (j - i) / 2] * Math.sqrt(Math.abs(p.max - prof[j + 1]) * inv);
        height += w * prof[j + 1];
        total += w;
      }
      let h = Math.min(p.max, Math.max(p.min, total === 0 ? hi : height / total));
      if (keepMin) h = Math.min(h, hi);
      out.push(zi, h);
    }
    return out;
  };
  let out = profile;
  for (let k = 0; k < 6; k++) out = blur(out);
  return out;
}



export type EditAction = 'increase' | 'decrease' | 'reduce' | 'smooth';


export function adjustProfile(profile: number[], p: SlicingParams, z: number, delta: number, band: number, action: EditAction): number[] {
  const lo0 = p.first, hi0 = p.height;
  if (z < lo0 || z > hi0 || profile.length < 2) return profile;
  let current = p.layer;
  for (let i = 0; i < profile.length; i += 2) {
    if (i + 2 === profile.length) { current = profile[i + 1]; break; }
    if (profile[i + 2] > z) { current = profile[i + 1] + (profile[i + 3] - profile[i + 1]) * (z - profile[i]) / (profile[i + 2] - profile[i]); break; }
  }
  if (action === 'decrease') delta = -delta;
  if (action === 'increase' || action === 'decrease') {
    if (delta > 0) { if (current >= p.max - EPSILON) return profile; delta = Math.min(delta, p.max - current); }
    else { if (current <= p.min + EPSILON) return profile; delta = Math.max(delta, p.min - current); }
  } else {
    delta = Math.min(Math.abs(delta), Math.abs(p.layer - current));
    if (delta < EPSILON) return profile;
  }
  const lo = Math.max(lo0, z - 0.5 * band), hi = z + 0.5 * band, step = 0.1;
  let idx = 0;
  while (idx < profile.length && profile[idx] < lo) idx += 2;
  idx -= 2;
  const out = profile.slice(0, idx + 2);
  let zz = lo;
  const start = out.length;
  while (zz < hi) {
    const next = idx + 2;
    let height = profile[idx + 1];
    if (next < profile.length) height = profile[idx + 1] + (profile[next + 1] - profile[idx + 1]) * (zz - profile[idx]) / (profile[next] - profile[idx]);
    const w = Math.abs(zz - z) < 0.5 * band ? 0.5 + 0.5 * Math.cos((2 * Math.PI * (zz - z)) / band) : 0;
    if (action === 'increase' || action === 'decrease') height += w * delta;
    else if (action === 'reduce') {
      const d = height - p.layer, s = w * delta;
      height += Math.abs(d) > s ? (d > 0 ? -s : s) : -d;
    }
    height = Math.min(p.max, Math.max(p.min, height));
    if (zz === hi0) {
      if (out[out.length - 2] + EPSILON > zz) out.splice(-2);
      out.push(zz, height);
      idx = profile.length;
      break;
    }
    if (out[out.length - 2] + EPSILON < zz) out.push(zz, height);
    zz = Math.min(zz + step, hi0);
    idx = next;
    while (idx < profile.length && profile[idx] < zz) idx += 2;
    idx -= 2;
  }
  idx += 2;
  let end = out.length;
  if (idx < profile.length) out.push(...profile.slice(idx));
  else if (out[out.length - 2] + 0.5 * EPSILON < hi0) out.push(...profile.slice(-2));
  if (action === 'smooth') {
    let s0 = start === 0 ? 1 : start;
    if (end === out.length) end -= 2;
    for (let round = 0; round < 6; round++) {
      const prev = out.slice();
      for (let i = s0; i < end; i += 2) {
        const t = Math.abs(prev[i] - z) < 0.5 * band ? 0.25 + 0.25 * Math.cos((2 * Math.PI * (prev[i] - z)) / band) : 0;
        if (i === 0) out[i + 1] = (1 - t) * prev[i + 1] + t * prev[i + 3];
        else if (i + 1 === prev.length) out[i + 1] = (1 - t) * prev[i + 1] + t * prev[i - 1];
        else out[i + 1] = (1 - t) * prev[i + 1] + 0.5 * t * (prev[i - 1] + prev[i + 3]);
      }
    }
  }
  return out;
}


export function heightAt(profile: number[], z: number): number {
  for (let i = 0; i + 2 < profile.length; i += 2)
    if (profile[i + 2] > z) return profile[i + 1] + (profile[i + 3] - profile[i + 1]) * (z - profile[i]) / (profile[i + 2] - profile[i]);
  return profile[profile.length - 1];
}






export function fitProfile(profile: number[], height: number, first: number): number[] {
  const top = profile[profile.length - 2];
  if (!(top > 0) || !(height > 0)) return profile;
  const k = height / top;
  const out = [0, first, first, first];
  for (let i = 0; i < profile.length; i += 2) {
    const z = Math.round(profile[i] * k * 1e4) / 1e4;
    if (z > first + EPSILON) out.push(z, Math.round(profile[i + 1] * 1e4) / 1e4);
  }
  if (out[out.length - 2] < height - EPSILON) out.push(Math.round(height * 1e4) / 1e4, out[out.length - 1]);
  else out[out.length - 2] = Math.round(height * 1e4) / 1e4;
  return out;
}
