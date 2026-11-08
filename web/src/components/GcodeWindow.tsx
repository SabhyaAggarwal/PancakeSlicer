import { useEffect, useState } from 'react';
import { Glyph } from './Glyph';
import { unzipSync } from 'fflate';
import { useStore } from '../state/store';

const ROWS = 15;
const BLOCK = 1024;
interface Shown { first: number; lines: string[] }
type Reader = (line: number) => Promise<Shown>;
let cache: { key: string; read: Reader } | null = null;

const around = (total: number, line: number) => Math.max(1, Math.min(line - Math.floor(ROWS / 2), total - ROWS + 1));

async function text(blob: Blob): Promise<string> {
  const head = new Uint8Array(await blob.slice(0, 2).arrayBuffer());
  if (head[0] !== 0x50 || head[1] !== 0x4b) return blob.text();
  const files = unzipSync(new Uint8Array(await blob.arrayBuffer()), { filter: (f) => /^Metadata\/plate_\d+\.gcode$/.test(f.name) });
  const name = Object.keys(files).sort()[0];
  if (!name) throw new Error('no plate G-code in the package');
  return new TextDecoder().decode(files[name]);
}

function fileReader(file: File): Reader {
  const all = text(file).then((t) => t.split('\n'));
  return async (line) => {
    const lines = await all, first = around(lines.length, line);
    return { first, lines: lines.slice(first - 1, first - 1 + ROWS) };
  };
}

function jobReader(id: string): Reader {
  const blocks = new Map<number, Promise<{ total: number; lines: string[] }>>();
  const block = (k: number) => {
    let b = blocks.get(k);
    if (!b) {
      const read = fetch(`/api/jobs/${encodeURIComponent(id)}/gcode-lines?block=${k}`).then(async (r) => {
        if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
        return r.json() as Promise<{ total: number; lines: string[] }>;
      });
      read.catch(() => { if (blocks.get(k) === read) blocks.delete(k); });
      blocks.set(k, (b = read));
    }
    return b;
  };
  return async (line) => {
    const { total } = await block(Math.floor(Math.max(0, line - 1) / BLOCK));
    const first = around(total, line), last = Math.min(total, first + ROWS - 1);
    const k0 = Math.floor((first - 1) / BLOCK), k1 = Math.floor((last - 1) / BLOCK);
    const parts = await Promise.all(Array.from({ length: k1 - k0 + 1 }, (_, i) => block(k0 + i)));
    if ((k1 + 1) * BLOCK < total) block(k1 + 1).catch(() => {});
    const at = first - 1 - k0 * BLOCK;
    return { first, lines: parts.flatMap((p) => p.lines).slice(at, at + ROWS) };
  };
}

export function GcodeWindow({ line, file, onClose }: { line: number; file?: File | null; onClose(): void }) {
  const [shown, setShown] = useState<Shown | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);
  const job = useStore((s) => s.job.id);
  const key = file ? `file:${file.name}:${file.size}` : job ? `job:${job}` : null;
  useEffect(() => {
    setShown(null);
    setError(null);
  }, [key, attempt]);
  useEffect(() => {
    if (!key) return;
    if (cache?.key !== key) cache = { key, read: file ? fileReader(file) : jobReader(job!) };
    const read = cache.read;
    let live = true;
    read(line).then((w) => live && setShown(w), (e) => {
      if (cache?.read === read) cache = null;
      if (live) setError(String(e instanceof Error ? e.message : e));
    });
    return () => { live = false; };
  }, [key, line, attempt]);
  return (
    <section className="gcode-window" aria-label="G-code">
      <header className="gcode-head">
        <span className="gcode-title">G-code</span>
        {shown && line > 0 && <span className="gcode-at">Line {line.toLocaleString()}</span>}
        <button type="button" className="icon-btn gcode-close" aria-label="Close G-code" onClick={onClose}><Glyph name="close" size={14} /></button>
      </header>
      {error ? (
        <div className="gcode-status" role="status">
          <span>Couldn't read the G-code</span>
          <button type="button" className="btn-ghost" onClick={() => setAttempt((n) => n + 1)}>Retry</button>
        </div>
      ) : !shown ? (
        <div className="gcode-lines" role="status" aria-label="Loading G-code">{Array.from({ length: ROWS }, (_, k) => <div key={k} className="gcode-line"><span className="skel-line" /></div>)}</div>
      ) : (
        <div className="gcode-lines">
          {shown.lines.map((l, k) => (
            <div key={shown.first + k} className={`gcode-line${shown.first + k === line ? ' current' : ''}`} aria-current={shown.first + k === line ? 'true' : undefined}>
              <span className="gcode-no">{shown.first + k}</span><span className="gcode-text">{l}</span>
            </div>
          ))}
        </div>
      )}
    </section>
  );
}

export default GcodeWindow;
