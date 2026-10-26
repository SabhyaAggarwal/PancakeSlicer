import { arrangeAll, importFiles } from '../actions';
import { toast } from '../state/store';

export const HANDY: [label: string, files: string[], arrange?: boolean][] = [
  ['Orca Cube', ['OrcaCube_v2.drc', 'OrcaPlug_v2.drc'], true],
  ['OrcaSliced Combo', ['OrcaSliced.3mf', 'OrcaCube_v2.drc', 'OrcaPlug_v2.drc'], true],
  ['Orca Badge', ['OrcaBadge.3mf']],
  ['Orca Tolerance Test', ['OrcaToleranceTest.drc']],
  ['3DBenchy', ['3DBenchy.drc']],
  ['Cali Cat', ['calicat.drc']],
  ['Autodesk FDM Test', ['ksr_fdmtest_v4.drc']],
  ['Voron Cube', ['Voron_Design_Cube_v7.drc']],
  ['Stanford Bunny', ['Stanford_Bunny.drc']],
  ['Orca String Hell', ['Orca_stringhell.drc']],
];

export async function addHandyModel(i: number) {
  const [label, names, arrange] = HANDY[i];
  try {
    const files = await Promise.all(names.map(async (name) => {
      const r = await fetch(`/api/handy-models/${encodeURIComponent(name)}`);
      if (!r.ok) throw new Error(`${label}: ${r.status} ${r.statusText}`);
      return new File([await r.blob()], name);
    }));
    await importFiles(files, true);
    if (arrange) arrangeAll(false);
  } catch (e) {
    toast(e instanceof Error ? e.message : String(e));
  }
}
