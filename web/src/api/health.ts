

const FRESH_MS = 2000;
let last: { read: Promise<Response>; at: number } | null = null;


export interface EngineCapabilities { options: boolean; presets: boolean }
let capabilities: EngineCapabilities | null = null, settleCapabilities!: (value: EngineCapabilities) => void;
const firstCapabilities = new Promise<EngineCapabilities>((resolve) => { settleCapabilities = resolve; });

export function noteCapabilities(features: Record<string, unknown>) {
  capabilities = { options: features.dump_options === true, presets: features.dump_presets === true };
  settleCapabilities(capabilities);
}

export const engineCapabilities = () => capabilities ? Promise.resolve(capabilities) : firstCapabilities;

export function readHealth(timeoutMs: number): Promise<Response> {
  if (!last || performance.now() - last.at > FRESH_MS) {
    const read = fetch('/api/healthz', { cache: 'no-store', signal: AbortSignal.timeout(timeoutMs) });
    const entry = { read, at: performance.now() };
    last = entry;
    void read.then((r) => { if (r.ok) entry.at = performance.now(); else if (last === entry) last = null; }, () => { if (last === entry) last = null; });
  }
  return last.read.then((r) => r.clone());
}
