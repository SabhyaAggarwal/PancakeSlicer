import type { ConfigValue, PresetType } from '../api/types';
import type { State } from '../state/store';

export interface ProfileConflict { key: string; mine: ConfigValue | undefined; theirs: ConfigValue | undefined }

export const selectProfileConflicts = (_s: State, _type: PresetType, _name?: string): ProfileConflict[] => [];
export async function resolveProfileConflict(_type: PresetType, _name: string, _key: string, _side: 'mine' | 'theirs'): Promise<void> {}
