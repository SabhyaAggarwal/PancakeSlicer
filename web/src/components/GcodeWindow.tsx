import { useEffect, useState } from 'react';
import { unzipSync } from 'fflate';
import { getState } from '../state/store';

const ROWS = 15;
let cache: { key: string; lines: Promise<string[]> } | null = null;

async function text(blob: Blob): Promise<string> {
  const head = new Uint8Array(await blob.slice(0, 2).arrayBuffer());
  if (head[0] !== 0x50 || head[1] !== 0x4b) return blob.text();
  const files = unzipSync(new Uint8Array(await blob.arrayBuffer()), { filter: (f) => /^Metadata\/plate_\d+\.gcode$/.test(f.name) });
  const name = Object.keys(files).sort()[0];
  if (!name) throw new Error('no plate G-code in the package');
  return new TextDecoder().decode(files[name]);
}

function lines(key: string, load: () => Promise<Blob>): Promise<string[]> {
  if (cache?.key !== key) cache = { key, lines: load().then(text).then((t) => t.split('\n')) };
  return cache.lines;
}

export function GcodeWindow({ line, file }: { line: number; file?: File | null }) {
  const [all, setAll] = useState<string[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const job = getState().job.id;
  useEffect(() => {
    const s = getState();
    const key = file ? `file:${file.name}:${file.size}` : `job:${s.job.id}`;
    const load = file ? () => Promise.resolve(file as Blob) : async () => {
      const r = await fetch(s.api!.downloadUrl(s.job.id!));
      if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
      return r.blob();
    };
    if (!file && (!s.job.id || !s.api)) return;
    let live = true;
    setAll(null);
    setError(null);
    lines(key, load).then((l) => live && setAll(l), (e) => live && setError(String(e instanceof Error ? e.message : e)));
    return () => { live = false; };
  }, [file, job]);
  if (error) return <div className="gcode-window" role="status">G-code window: {error}</div>;
  if (!all || !(line > 0)) return null;
  const first = Math.max(1, Math.min(line - Math.floor(ROWS / 2), all.length - ROWS + 1));
  return (
    <div className="gcode-window" aria-label="G-code window">
      {all.slice(first - 1, first - 1 + ROWS).map((l, k) => (
        <div key={first + k} className={`gcode-line${first + k === line ? ' current' : ''}`}>
          <span className="gcode-no">{first + k}</span>{l}
        </div>
      ))}
    </div>
  );
}
