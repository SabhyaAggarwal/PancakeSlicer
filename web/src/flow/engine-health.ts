import { readHealth } from '../api/health';
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
let probe: Promise<void> | null = null;
export function probeEngine(): Promise<void> {
  if (probe) return probe;
  engineHealth.send({ type: 'RETRY' });
  probe = readHealth(5000).then(async (r) => {
    if (!r.ok) throw new Error('Health unavailable');
    engineHealth.send({ type: 'HEALTH', health: await r.json() as EngineHealth });
  }).catch(() => engineHealth.send({ type: 'FAIL' })).finally(() => { probe = null; });
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
