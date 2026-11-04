import type { Config, PresetType } from '../api/types';
import type { WorksWithRule } from './works-with.ts';

export interface ChainLink { type: PresetType; id?: string; name?: string; overlay: Config; applicability?: WorksWithRule; auto?: AutoRecord }
export interface AutoRecord { schema?: 1; preferredHeight?: number; preferredSpeed?: 'quality' | 'balanced' | 'fast' }

export const chainApplicability = (links: ChainLink[]) => [...links].reverse().find((l) => l.applicability)?.applicability;
export function foldBase<P extends ChainLink>(base: ChainLink, child: P): P {
  const applicability = child.applicability ?? base.applicability;
  return { ...child, overlay: { ...base.overlay, ...child.overlay }, ...(applicability ? { applicability } : {}) };
}
export function resetToBase<P extends ChainLink>(p: P, key: string): P {
  const overlay = { ...p.overlay };
  delete overlay[key];
  return { ...p, overlay };
}
