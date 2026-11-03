import { useEffect, useState } from 'react';
import { Glyph } from './Glyph';
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
  if (cache?.key !== key) {
    const read = load().then(text).then((t) => t.split('\n'));
    cache = { key, lines: read };
    read.catch(() => { if (cache?.lines === read) cache = null; });
  }
  return cache.lines;
}

export function GcodeWindow({ line, file, onClose }: { line: number; file?: File | null; onClose(): void }) {
  const [all, setAll] = useState<string[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);
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
  }, [file, job, attempt]);
  const first = all ? Math.max(1, Math.min(line - Math.floor(ROWS / 2), all.length - ROWS + 1)) : 1;
  return (
    <section className="gcode-window" aria-label="G-code">
      <header className="gcode-head">
        <span className="gcode-title">G-code</span>
        {all && line > 0 && <span className="gcode-at">Line {line.toLocaleString()}</span>}
        <button type="button" className="icon-btn gcode-close" aria-label="Close G-code" onClick={onClose}><Glyph name="close" size={14} /></button>
      </header>
      {error ? (
        <div className="gcode-status" role="status">
          <span>Couldn't read the G-code</span>
          <button type="button" className="btn-ghost" onClick={() => setAttempt((n) => n + 1)}>Retry</button>
        </div>
      ) : !all ? (
        <div className="gcode-lines" role="status" aria-label="Loading G-code">{Array.from({ length: ROWS }, (_, k) => <div key={k} className="gcode-line"><span className="skel-line" /></div>)}</div>
      ) : (
        <div className="gcode-lines">
          {all.slice(first - 1, first - 1 + ROWS).map((l, k) => (
            <div key={first + k} className={`gcode-line${first + k === line ? ' current' : ''}`} aria-current={first + k === line ? 'true' : undefined}>
              <span className="gcode-no">{first + k}</span><span className="gcode-text">{l}</span>
            </div>
          ))}
        </div>
      )}
    </section>
  );
}

export default GcodeWindow;
