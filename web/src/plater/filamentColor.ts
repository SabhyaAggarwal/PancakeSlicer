import type { State } from '../state/store';
import { slotColour } from '../profiles/filament-colour';

export const filamentColor = (s: State, extruder: number): string => slotColour(s, extruder - 1);
