import { mutation, type ApiError } from './access';
import { sha256 } from '../sha256';

export interface JobFile { name: string; blob: Blob }

const digests = new WeakMap<Blob, Promise<string>>();
const present = new Set<string>();
const formFiles = new WeakMap<FormData, Map<string, Blob>>();

export function blobDigest(blob: Blob): Promise<string> {
  let digest = digests.get(blob);
  if (!digest) {
    digest = sha256(blob);
    digests.set(blob, digest);
    digest.catch(() => digests.delete(blob));
  }
  return digest;
}

async function upload(blobs: Map<string, Blob>, signal?: AbortSignal, onUpload?: (fraction: number) => void) {
  const total = [...blobs.values()].reduce((n, b) => n + b.size, 0) || 1;
  const sent = new Map<string, number>();
  const report = () => onUpload?.([...sent.values()].reduce((a, b) => a + b, 0) / total);
  const queue = [...blobs];
  await Promise.all(Array.from({ length: Math.min(3, queue.length) }, async () => {
    for (let next = queue.shift(); next; next = queue.shift()) {
      const [sum, blob] = next;
      if (signal?.aborted) throw new Error('Slicing canceled');
      await mutation(`/api/blobs/${sum}`, { method: 'PUT', body: blob }, false, signal, (f) => { sent.set(sum, f * blob.size); report(); });
      sent.set(sum, blob.size);
      present.add(sum);
      report();
    }
  }));
}

export async function attachFiles(fd: FormData, files: JobFile[], signal?: AbortSignal, onUpload?: (fraction: number) => void) {
  const sums = await Promise.all(files.map((f) => blobDigest(f.blob)));
  const blobs = new Map(sums.map((sum, i) => [sum, files[i].blob]));
  const unknown = [...blobs.keys()].filter((sum) => !present.has(sum));
  if (unknown.length) {
    const r = await mutation('/api/blobs/missing', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ sha256: unknown }) });
    const { missing } = await r.json() as { missing: string[] };
    for (const sum of unknown) if (!missing.includes(sum)) present.add(sum);
    await upload(new Map(missing.map((sum) => [sum, blobs.get(sum)!])), signal, onUpload);
  }
  onUpload?.(1);
  fd.set('files', JSON.stringify(files.map((f, i) => ({ name: f.name, sha256: sums[i] }))));
  formFiles.set(fd, blobs);
}

export async function postJob(fd: FormData, url = '/api/jobs', init: { headers?: HeadersInit; retry?: boolean; signal?: AbortSignal; quiet?: boolean } = {}) {
  const post = () => mutation(url, { method: 'POST', headers: init.headers, body: fd }, init.retry, init.signal, undefined, init.quiet);
  try {
    return await post();
  } catch (error) {
    const blobs = formFiles.get(fd);
    if (!blobs || (error as ApiError).code !== 'blob_missing') throw error;
    const missing = ((error as ApiError).missing ?? [...blobs.keys()]).filter((sum) => blobs.has(sum));
    missing.forEach((sum) => present.delete(sum));
    await upload(new Map(missing.map((sum) => [sum, blobs.get(sum)!])), init.signal);
    return post();
  }
}

export async function withBlobs<T>(meshes: Blob[], post: (sums: string[]) => Promise<T>, signal?: AbortSignal): Promise<T> {
  await attachFiles(new FormData(), meshes.map((blob, i) => ({ name: `mesh${i}.stl`, blob })), signal);
  const sums = await Promise.all(meshes.map(blobDigest));
  try {
    return await post(sums);
  } catch (error) {
    if ((error as ApiError).code !== 'blob_missing') throw error;
    const blobs = new Map(sums.map((sum, i) => [sum, meshes[i]]));
    const missing = ((error as ApiError).missing ?? sums).filter((sum) => blobs.has(sum));
    missing.forEach((sum) => present.delete(sum));
    await upload(new Map(missing.map((sum) => [sum, blobs.get(sum)!])), signal);
    return post(sums);
  }
}
