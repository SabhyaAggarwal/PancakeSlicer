import type { PresetType } from '../api/types';
import type { State } from '../state/store';
import { profileChain } from './context';
import { resolveProfile } from './model';

export const chainIsDynamic = (s: State, type: PresetType, key: string) => profileChain(s, type, resolveProfile(s, type, key)).some((l) => !!l.dynamic);
