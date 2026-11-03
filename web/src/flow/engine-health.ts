import { applyHealth, type SurfaceArtifactManifest } from '../api/client';
import { readHealth } from '../api/health';
import { getState, setState } from '../state/store';
import { assertNever, createMachine, useMachine } from './fsm';

export type EngineState = 'unknown' | 'healthy' | 'degraded' | 'qualified' | 'unqualified';
export interface EngineHealth {
  tool_features?: Record<string, unknown>;
  artifact_manifest?: { features?: Record<string, unknown>; binary_sha256?: string; resources_sha256?: string;
    validated?: boolean; preview_ready?: boolean; print_qualified?: boolean };
  engine_state?: string;
}
type HealthEvent = { type: 'HEALTH'; health: EngineHealth } | { type: 'FAIL' } | { type: 'RETRY' };
interface HealthContext { health: EngineHealth | null }
export function classifyEngine(health: EngineHealth): Exclude<EngineState, 'unknown'> {
  const manifest = health.artifact_manifest, tools = health.tool_features;
  if (!tools || health.engine_state === 'degraded' || health.engine_state === 'down') return 'degraded';
  if (!manifest) return 'healthy';
  const matches = Object.entries(manifest.features ?? {}).every(([key, value]) => value !== true || tools[key] === true)
    && ['curved_tops', 'nonplanar_wave'].every((key) => tools[key] !== true || manifest.features?.[key] === true);
  const hashes = /^[a-f0-9]{64}$/i.test(manifest.binary_sha256 ?? '') && /^[a-f0-9]{64}$/i.test(manifest.resources_sha256 ?? '');
  return matches && hashes && manifest.print_qualified === true ? 'qualified' : 'unqualified';
}
const healthTransitions = (['healthy', 'degraded', 'qualified', 'unqualified'] as const).map((target) => ({
  target, guard: (_: HealthContext, e: Extract<HealthEvent, { type: 'HEALTH' }>) => classifyEngine(e.health) === target,
  action: (_: HealthContext, e: Extract<HealthEvent, { type: 'HEALTH' }>) => ({ health: e.health }),
}));
const on = { HEALTH: healthTransitions, FAIL: 'degraded', RETRY: 'unknown' } as const;
export const engineHealth = createMachine<EngineState, HealthEvent, HealthContext>({
  id: 'engine-health', initial: 'unknown', context: { health: null },
  states: { unknown: { on }, healthy: { on }, degraded: { on }, qualified: { on }, unqualified: { on } },
});
let probe: Promise<void> | null = null, retry: ReturnType<typeof setTimeout> | undefined, watching = false;
function adopt(health: EngineHealth) {
  applyHealth(health as { tool_features?: Record<string, unknown>; artifact_manifest?: SurfaceArtifactManifest });
  if (health.tool_features && !getState().api?.mock) setState({ features: { ...health.tool_features } });
}
function watch() {
  if (watching) return;
  watching = true;
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') void probeEngine(); });
  window.addEventListener('online', () => void probeEngine());
}
export function probeEngine(): Promise<void> {
  if (probe) return probe;
  watch();
  clearTimeout(retry);
  engineHealth.send({ type: 'RETRY' });
  probe = readHealth(5000).then(async (r) => {
    if (!r.ok) throw new Error('Health unavailable');
    const health = await r.json() as EngineHealth;
    adopt(health);
    engineHealth.send({ type: 'HEALTH', health });
  }).catch(() => engineHealth.send({ type: 'FAIL' })).finally(() => {
    probe = null;
    const { value, context } = engineHealth.get();
    const tools = context.health?.tool_features, partial = !context.health?.artifact_manifest && (tools?.curved_tops === true || tools?.nonplanar_wave === true);
    if (value === 'degraded' || partial) retry = setTimeout(() => void probeEngine(), 10000);
  });
  return probe;
}
export function enginePermission(): 'unknown' | 'preview' | 'print' | 'unavailable' {
  const state = engineHealth.get().value;
  switch (state) {
    case 'unknown': return 'unknown';
    case 'healthy': case 'unqualified': return 'preview';
    case 'qualified': return 'print';
    case 'degraded': return 'unavailable';
    default: return assertNever(state);
  }
}
export const useEngineHealth = () => useMachine(engineHealth);
