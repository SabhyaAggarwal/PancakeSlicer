import type { Config } from '../api/types';
import type { Doc } from '../state/history';
import type { State } from '../state/store';
import type { SliceSummary } from './slice';
export interface SpeculateOptions { signal?: AbortSignal; config?: Config }
export function speculate(_edit: (s: State) => Partial<Doc>, _opts: SpeculateOptions = {}): Promise<SliceSummary | null> {
  return Promise.resolve(null);
}
