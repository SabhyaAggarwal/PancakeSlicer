import { responseError } from './access';

export interface JobStreamEvent { type: string; data: string; seq: number }
interface Follower { after: number; event(e: JobStreamEvent): void; end(error?: unknown): void }
interface Stream { followers: Set<Follower>; events: JobStreamEvent[]; ctl: AbortController }

const streams = new Map<string, Stream>();

async function read(id: string, stream: Stream, after: number) {
  let error: unknown;
  try {
    const r = await fetch(`/api/jobs/${encodeURIComponent(id)}/events`, { signal: stream.ctl.signal,
      headers: after >= 0 ? { 'Last-Event-ID': String(after), Accept: 'text/event-stream' } : { Accept: 'text/event-stream' } });
    if (!r.ok) throw await responseError(r);
    if (!r.body || !r.headers.get('content-type')?.includes('text/event-stream')) throw new Error('Job stream unavailable');
    const reader = r.body.getReader(), decoder = new TextDecoder();
    let buffer = '', last = after;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        buffer = (buffer + decoder.decode(value, { stream: !done })).replace(/\r\n/g, '\n');
        let at: number;
        while ((at = buffer.indexOf('\n\n')) >= 0) {
          const block = buffer.slice(0, at); buffer = buffer.slice(at + 2);
          let seq = -1, type = 'message';
          const data: string[] = [];
          for (const line of block.split('\n')) {
            if (line.startsWith('id:')) seq = Number(line.slice(3).trim());
            else if (line.startsWith('event:')) type = line.slice(6).trim();
            else if (line.startsWith('data:')) data.push(line.slice(5).trimStart());
          }
          if (!data.length || !Number.isSafeInteger(seq) || seq <= last) continue;
          last = seq;
          const e = { type, data: data.join('\n'), seq };
          stream.events.push(e);
          for (const f of [...stream.followers]) if (seq > f.after) { f.after = seq; f.event(e); }
        }
        if (buffer.length > 1_048_576) throw new Error('Job event exceeds stream limit');
        if (done) break;
      }
    } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
  } catch (e) { error = e; }
  if (streams.get(id) === stream) streams.delete(id);
  for (const f of [...stream.followers]) f.end(stream.ctl.signal.aborted ? undefined : error);
}

export function followJobStream(id: string, after: number, event: (e: JobStreamEvent) => void, end: (error?: unknown) => void): () => void {
  let stream = streams.get(id);
  const follower: Follower = { after, event, end };
  if (!stream) {
    stream = { followers: new Set(), events: [], ctl: new AbortController() };
    streams.set(id, stream);
    stream.followers.add(follower);
    void read(id, stream, after);
  } else {
    stream.followers.add(follower);
    for (const e of stream.events) if (e.seq > follower.after) { follower.after = e.seq; event(e); }
  }
  const own = stream;
  return () => {
    if (!own.followers.delete(follower) || own.followers.size) return;
    if (streams.get(id) === own) streams.delete(id);
    own.ctl.abort();
  };
}
