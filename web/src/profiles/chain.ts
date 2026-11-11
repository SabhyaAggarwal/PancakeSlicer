import type { Config, PresetType } from '../api/types';
import type { WorksWithRule } from './works-with.ts';
import type { AutoRecord } from './auto-rules';

export interface ChainLink { type: PresetType; id?: string; name?: string; overlay: Config; applicability?: WorksWithRule; auto?: AutoRecord }

export const chainApplicability = (links: ChainLink[]) => [...links].reverse().find((l) => l.applicability)?.applicability;
