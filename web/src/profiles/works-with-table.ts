import '../flush-state';
import type { Config, PresetType, SystemPreset, UserPreset } from '../api/types';
import type { State } from '../state/store';
import { selectCompatibility } from './compatibility';
import { listProfiles } from './catalog';
import { effectiveProfileValues, processHeights } from './context';
import type { WorksWithRule } from './works-with';
import type { DynamicProfile } from './dynamic';

const base = (type: PresetType, name: string, config: Config): SystemPreset => ({ name, version: '1', vendor: 'BBL', type, config });
const owned = (type: PresetType, id: string, baseName: string, extra: { applicability?: WorksWithRule; dynamic?: DynamicProfile; parentId?: string; baseRole?: 'base' | 'base-only'; overlay?: Config } = {}) =>
  ({ id: `profile:${id}`, type, kind: 'profile', name: id, revision: 1, base: { name: baseName, version: '1', vendor: 'BBL' },
    baseline: {}, ...extra, overlay: extra.overlay ?? {}, overrides: extra.overlay ?? {} }) as unknown as UserPreset;
const rule = (r: Omit<WorksWithRule, 'schema'>): WorksWithRule => ({ schema: 2, ...r });

const machines = [
  base('machine', 'A1 0.4', { printer_model: 'Bambu Lab A1', nozzle_diameter: ['0.4'] }),
  base('machine', 'A1 0.6', { printer_model: 'Bambu Lab A1', nozzle_diameter: ['0.6'] }),
  base('machine', 'A1 0.2', { printer_model: 'Bambu Lab A1', nozzle_diameter: ['0.2'] }),
  base('machine', 'A1 1.0', { printer_model: 'Bambu Lab A1', nozzle_diameter: ['1.0'] }),
  base('machine', 'A1 mini 0.4', { printer_model: 'Bambu Lab A1 mini', nozzle_diameter: ['0.4'] }),
];
const processes = [base('process', 'Standard', { layer_height: '0.2' })];
const filaments = [base('filament', 'Generic PLA', { filament_type: ['PLA'] }), base('filament', 'Generic PETG', { filament_type: ['PETG'] })];
const library: UserPreset[] = [
  owned('machine', 'a1-04', 'A1 0.4'), owned('machine', 'a1-06', 'A1 0.6'), owned('machine', 'a1-02', 'A1 0.2'),
  owned('machine', 'a1-10', 'A1 1.0'), owned('machine', 'mini-04', 'A1 mini 0.4'),
  owned('machine', 'a1-04-pla', 'A1 0.4', { applicability: rule({ filaments: { types: ['PLA'] } }) }),
  owned('process', 'any', 'Standard'),
  owned('process', 'a1-models', 'Standard', { applicability: rule({ printers: { models: ['Bambu Lab A1'] } }) }),
  owned('process', 'mini-only', 'Standard', { applicability: rule({ printers: { printerIds: ['profile:mini-04'] } }) }),
  owned('process', 'nozzle-04', 'Standard', { applicability: rule({ nozzle: { sizes: [0.4] } }) }),
  owned('process', 'nozzle-03-06', 'Standard', { applicability: rule({ nozzle: { min: 0.3, max: 0.6 } }) }),
  owned('process', 'a1-not-06', 'Standard', { applicability: rule({ printers: { models: ['Bambu Lab A1'] }, exclude: ['profile:a1-06'] }) }),
  owned('process', 'pla-only', 'Standard', { applicability: rule({ filaments: { types: ['PLA'] } }) }),
  owned('process', 'dyn', 'Standard', { dynamic: { schema: 1, reference: { nozzle: 0.4, layerHeight: 0.2 }, layers: [
    { id: 'layer:fine', when: { layerHeight: 0.12 }, values: {} }] } }),
  owned('process', 'dyn-base', 'Standard', { baseRole: 'base-only', applicability: rule({ printers: { models: ['Bambu Lab A1'] } }),
    overlay: { wall_loops: '2' }, dynamic: { schema: 1, reference: { nozzle: 0.4, layerHeight: 0.2 }, layers: [{ id: 'layer:b', when: { layerHeight: 0.12 }, values: { wall_loops: '3', top_shell_layers: '7' } }] } }),
  owned('process', 'dyn-child', 'Standard', { parentId: 'profile:dyn-base', overlay: { wall_loops: '5' } }),
  owned('process', 'norm-base', 'Standard', { baseRole: 'base', overlay: { wall_loops: '4' } }),
  owned('process', 'norm-child', 'Standard', { parentId: 'profile:norm-base' }),
  owned('filament', 'pla', 'Generic PLA'), owned('filament', 'petg', 'Generic PETG'),
  owned('filament', 'pla-for-any', 'Generic PLA', { applicability: rule({ prints: { profileIds: ['profile:any'] } }) }),
  owned('filament', 'pla-mini', 'Generic PLA', { applicability: rule({ printers: { models: ['Bambu Lab A1 mini'] } }) }),
];
const state = (printer: string, process: string, slots: string[], layerHeight?: number) => ({
  machines, processes, filaments, userPresets: library, printer: `profile:${printer}`, process: `profile:${process}`,
  filamentSlots: slots.map((s) => `profile:${s}`), profileSnapshots: null, projectPresets: null, dirty: { machine: {}, process: {} },
  filamentEdits: {}, projectPlates: [], plateIndex: 1, processVariant: layerHeight ? { layerHeight } : null,
}) as unknown as State;
const offered = (s: State, type: PresetType) => listProfiles(s, type).map((r) => r.name).sort();

export function worksWithTable(): [string, unknown, unknown][] {
  const ok = (s: State, type: PresetType, id: string) => selectCompatibility(s, type, `profile:${id}`).ok;
  const rows: [string, unknown, unknown][] = [];
  const processesOn = (printer: string, slots = ['pla']) => offered(state(printer, 'any', slots), 'process');
  rows.push(['print profiles on A1 0.4 with PLA (base-only hidden)', processesOn('a1-04'), ['a1-models', 'a1-not-06', 'any', 'dyn', 'dyn-child', 'norm-base', 'norm-child', 'nozzle-03-06', 'nozzle-04', 'pla-only']]);
  rows.push(['print profiles on A1 0.6 (excluded by id, nozzle size)', processesOn('a1-06'), ['a1-models', 'any', 'dyn', 'dyn-child', 'norm-base', 'norm-child', 'nozzle-03-06', 'pla-only']]);
  rows.push(['print profiles on A1 0.2 (range, dynamic 0.12 only)', processesOn('a1-02'), ['a1-models', 'a1-not-06', 'any', 'dyn', 'dyn-child', 'norm-base', 'norm-child', 'pla-only']]);
  rows.push(['print profiles on A1 1.0 (dynamic has no valid height)', processesOn('a1-10'), ['a1-models', 'a1-not-06', 'any', 'norm-base', 'norm-child', 'pla-only']]);
  rows.push(['print profiles on A1 mini 0.4 (individual printer; the child takes its base rule)', processesOn('mini-04'), ['any', 'dyn', 'mini-only', 'norm-base', 'norm-child', 'nozzle-03-06', 'nozzle-04', 'pla-only']]);
  const walls = (process: string, h?: number) => { const v = effectiveProfileValues(state('a1-04', process, ['pla'], h), 'process', `profile:${process}`); return [v.wall_loops, v.top_shell_layers, v.layer_height]; };
  rows.push(['a chain child: its own key beats the base layer, the layer still adds the rest', walls('dyn-child', 0.12), ['5', '7', '0.12']]);
  rows.push(['a chain child at the reference height', walls('dyn-child'), ['5', undefined, '0.2']]);
  rows.push(['a chain child offers the base heights', processHeights(state('a1-04', 'dyn-child', ['pla'])), [0.12, 0.2]]);
  rows.push(['a normal chain child inherits its base values', walls('norm-child').slice(0, 1), ['4']]);
  rows.push(['print profiles with PETG loaded drop pla-only', processesOn('a1-04', ['petg']).includes('pla-only'), false]);
  rows.push(['print profiles with pla-for-any loaded: only "any" passes its prints condition',
    processesOn('a1-04', ['pla-for-any']), ['any']]);
  rows.push(['filaments with print "any" on A1 0.4', offered(state('a1-04', 'any', ['pla']), 'filament'), ['petg', 'pla', 'pla-for-any']]);
  rows.push(['filaments with print "a1-models" on A1 0.4 (prints condition)', offered(state('a1-04', 'a1-models', ['pla']), 'filament'), ['petg', 'pla']]);
  rows.push(['filaments on A1 mini (printer model rule)', offered(state('mini-04', 'any', ['pla']), 'filament'), ['petg', 'pla', 'pla-for-any', 'pla-mini']]);
  rows.push(['filaments with print "pla-only" (its filament condition; pla-for-any needs print "any")', offered(state('a1-04', 'pla-only', ['pla']), 'filament'), ['pla']]);
  rows.push(['filaments on a printer whose rule allows PLA', offered(state('a1-04-pla', 'any', ['pla']), 'filament'), ['pla', 'pla-for-any']]);
  rows.push(['the printer picker lists every printer', offered(state('a1-04', 'any', ['pla']), 'machine').length, 6]);
  rows.push(['a selected incompatible print profile is reported', ok(state('mini-04', 'a1-models', ['pla']), 'process', 'a1-models'), false]);
  rows.push(['dynamic heights on 0.4', processHeights(state('a1-04', 'dyn', ['pla'])), [0.12, 0.2]]);
  rows.push(['dynamic heights on 0.6', processHeights(state('a1-06', 'dyn', ['pla'])), [0.2]]);
  rows.push(['dynamic heights on 0.2', processHeights(state('a1-02', 'dyn', ['pla'])), [0.12]]);
  rows.push(['dynamic heights on 1.0', processHeights(state('a1-10', 'dyn', ['pla'])), []]);
  return rows;
}
