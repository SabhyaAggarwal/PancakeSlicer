import { useState, useSyncExternalStore } from 'react';
import type { Config, PresetType, UserPreset } from '../api/types';
import { Check } from '../components/Check';
import { Modal, ModalBody, ModalFooter, ModalTitle } from '../components/Modal';
import { getState, toast, useStore, type State } from '../state/store';
import { uniqueName } from './catalog';
import { printContext, profileChain } from './context';
import { availableHeights, chainRecord, foldBase, heightRange, mergeGroup, mergeProposals, sameMm, splitToNormal,
  type ChainLink, type DynamicContext, type DynamicProfile, type MergeCandidate } from './dynamic';
import { findProfile, profileKey } from './identity';
import { archiveProfile, restoreArchivedProfile } from './library-actions';
import { resolveProfile } from './model';
import type { WorksWithRule } from './works-with';

type Open = { kind: 'merge'; type: PresetType } | { kind: 'split'; type: PresetType; key: string } | null;
let open: Open = null;
const listeners = new Set<() => void>();
const show = (next: Open) => { open = next; listeners.forEach((l) => l()); };
export const openMergeSimilar = (type: PresetType) => show({ kind: 'merge', type });
export const openSplit = (type: PresetType, key: string) => show({ kind: 'split', type, key });
const close = () => show(null);

export function DynamicConversions() {
  const current = useSyncExternalStore((l) => { listeners.add(l); return () => { listeners.delete(l); }; }, () => open);
  if (!current) return null;
  return current.kind === 'merge' ? <MergeDialog type={current.type} /> : <SplitDialog type={current.type} profile={current.key} />;
}

const first = (value: Config[string] | undefined) => String(Array.isArray(value) ? value[0] ?? '' : value ?? '').split(/[;,]/)[0];
const dynamicOf = (p: UserPreset) => (p as { dynamic?: DynamicProfile }).dynamic;
const fresh = (p: UserPreset, id: string, name: string): UserPreset => {
  const { aliases: _a, provenance: _p, masterId: _m, printerId: _pr, archived: _ar, parentId: _pa, ...rest } = p as UserPreset & { mergedInto?: string; baseRole?: string };
  const { mergedInto: _mi, baseRole: _br, ...record } = rest as typeof rest & { mergedInto?: string; baseRole?: string };
  return { ...record, id, revision: 0, kind: 'profile', name } as UserPreset;
};
const newId = () => `profile:${crypto.randomUUID()}`;

function contextOf(s: State, p: UserPreset): MergeCandidate<UserPreset> | undefined {
  const resolved = resolveProfile(s, p.type, profileKey(p));
  if (resolved.issue) return undefined;
  const rule = p.applicability as WorksWithRule | undefined, machines: Config[] = [];
  if (rule?.printers && 'printerIds' in rule.printers) machines.push(...rule.printers.printerIds.map((id) => resolveProfile(s, 'machine', id).config));
  else if (!rule?.printers) for (const name of resolved.base?.compatible_printers ?? []) { const m = s.machines.find((x) => x.name === name); if (m) machines.push(m.config); }
  const models = rule?.printers && 'models' in rule.printers ? rule.printers.models : [...new Set(machines.map((m) => first(m.printer_model)))];
  const sizes = rule?.nozzle && 'sizes' in rule.nozzle ? rule.nozzle.sizes : [...new Set(machines.map((m) => Number(first(m.nozzle_diameter))))];
  if (models.length !== 1 || sizes.length !== 1 || !(sizes[0] > 0)) return undefined;
  const layerHeight = Number(first(resolved.config.layer_height)) || 0.2;
  return { profile: p, ctx: { printerModel: models[0], nozzle: sizes[0], layerHeight }, config: resolved.config };
}

function mergeGroups(s: State, type: PresetType) {
  const bases = new Set(s.userPresets.flatMap((p) => p.parentId ? [p.parentId] : []));
  const candidates = s.userPresets.filter((p) => p.type === type && p.kind !== 'master' && !p.archived && !p.parentId && !dynamicOf(p) && !bases.has(String(p.id)))
    .flatMap((p) => contextOf(s, p) ?? []);
  return mergeProposals(candidates).map((group) => ({ group, merged: mergeGroup(group, newId()) }));
}
const contexts = (type: PresetType, group: { ctx: DynamicContext }[]) => {
  const nozzles = [...new Set(group.map((g) => g.ctx.nozzle))].sort((a, b) => a - b).join(' / ');
  return type === 'process' ? `${nozzles} nozzle · ${heightRange(group.map((g) => g.ctx.layerHeight))} mm` : `${nozzles} nozzle`;
};

function MergeDialog({ type }: { type: PresetType }) {
  const [proposals] = useState(() => mergeGroups(getState(), type));
  const [checked, setChecked] = useState(() => proposals.map(() => true));
  const [busy, setBusy] = useState(false);
  const count = checked.filter(Boolean).length;
  const merge = async () => {
    setBusy(true);
    try {
      const s = getState(), operationId = `merge:${crypto.randomUUID()}`, chosen = proposals.filter((_, i) => checked[i]);
      const created = chosen.map(({ merged }) => fresh(merged, String(merged.id), uniqueName(s, type, merged.name)));
      const archived = chosen.flatMap(({ group }, i) => group.map((g) => ({ ...archiveProfile(s, type, profileKey(g.profile), operationId), mergedInto: created[i].id }) as UserPreset));
      const { commitProfileRecords } = await import('./transactions');
      await commitProfileRecords([...created, ...archived], { operationId });
      close();
      const undo = async () => {
        const now = getState();
        const restored = archived.flatMap((a) => {
          const p = findProfile(now, type, profileKey(a));
          if (p?.archived?.operationId !== operationId) return [];
          const { mergedInto: _m, ...rest } = restoreArchivedProfile(p) as UserPreset & { mergedInto?: string };
          return [rest as UserPreset];
        });
        const removed = created.flatMap((c) => { const p = findProfile(now, type, profileKey(c)); return p && !p.archived ? [archiveProfile(now, type, profileKey(p), operationId)] : []; });
        await commitProfileRecords([...restored, ...removed]).catch((e) => toast(e instanceof Error ? e.message : String(e), null, null, 'error'));
      };
      toast(`Merged ${archived.length} profiles into ${created.length}`, { label: 'Undo', run: () => void undo() });
    } catch (e) {
      toast(e instanceof Error ? e.message : String(e), null, null, 'error');
      setBusy(false);
    }
  };
  return <Modal size="m" label="Merge similar profiles" onClose={close}>
    <ModalTitle title="Merge similar profiles" onClose={close} />
    <ModalBody>
      {proposals.length ? <div className="modal-pick"><div className="modal-pick-list">{proposals.map(({ group, merged }, i) =>
        <label key={String(merged.id)} className="modal-check"><Check checked={checked[i]} onCheckedChange={(next) => setChecked(checked.map((c, j) => (j === i ? next : c)))} />
          <span>{merged.name} · {group.length} profiles · {contexts(type, group)}</span></label>)}</div></div>
        : <p>No similar profiles</p>}
    </ModalBody>
    <ModalFooter lead={proposals.length ? `${count} of ${proposals.length}` : undefined}>
      <button type="button" className="btn" onClick={close}>Cancel</button>
      {!!proposals.length && <button type="button" className="btn-primary" disabled={!count || busy} onClick={() => void merge()}>{count === 1 ? 'Merge 1 group' : `Merge ${count} groups`}</button>}
    </ModalFooter>
  </Modal>;
}

function splitSource(s: State, type: PresetType, key: string) {
  const resolved = resolveProfile(s, type, key), links = profileChain(s, type, resolved);
  if (!links.length || !links.some((l) => l.dynamic)) return undefined;
  const folded = links.slice(1).reduce<ChainLink>((base, child) => foldBase(base, child), links[0]) as ChainLink & UserPreset;
  const record = chainRecord(links), d = record.dynamic!, pc = printContext(s);
  const nozzles = [d.reference.nozzle, ...d.layers.flatMap((l) => l.when.nozzle ?? [])].filter((n, i, all) => all.findIndex((x) => sameMm(x, n)) === i).sort((a, b) => a - b);
  const list: DynamicContext[] = nozzles.flatMap((nozzle) => type === 'process'
    ? availableHeights(record, { ...pc, nozzle }).map((layerHeight) => ({ printerModel: pc.printerModel, nozzle, layerHeight }))
    : [{ printerModel: pc.printerModel, nozzle, layerHeight: d.reference.layerHeight }]);
  return { source: { ...resolved.source!, ...folded, dynamic: d, applicability: record.applicability } as UserPreset & ChainLink, below: resolved.base?.config ?? {}, list };
}
const contextLabel = (type: PresetType, c: DynamicContext) => type === 'process' ? `${c.nozzle} nozzle · ${c.layerHeight.toFixed(2)} mm` : `${c.nozzle} nozzle`;

function SplitDialog({ type, profile }: { type: PresetType; profile: string }) {
  const name = useStore((s) => findProfile(s, type, profile)?.name ?? '');
  const [split] = useState(() => splitSource(getState(), type, profile));
  const [checked, setChecked] = useState(() => { const nozzle = printContext(getState()).nozzle; return split?.list.map((c) => sameMm(c.nozzle, nozzle)) ?? []; });
  const [busy, setBusy] = useState(false);
  const count = checked.filter(Boolean).length;
  const run = async () => {
    if (!split) return;
    setBusy(true);
    try {
      const s = getState(), operationId = `split:${crypto.randomUUID()}`;
      const records = splitToNormal(split.source, split.below, split.list.filter((_, i) => checked[i]), newId).map((p) => {
        const record = fresh(p, String(p.id), uniqueName(s, type, p.name)), overlay = p.overlay ?? {};
        return { ...record, overlay, overrides: overlay, baseline: Object.fromEntries(Object.keys(overlay).map((k) => [k, split.below[k] ?? null])) } as UserPreset;
      });
      const { commitProfileRecords } = await import('./transactions');
      await commitProfileRecords(records, { operationId });
      close();
      toast(records.length === 1 ? `Added ${records[0].name}` : `Added ${records.length} profiles`);
    } catch (e) {
      toast(e instanceof Error ? e.message : String(e), null, null, 'error');
      setBusy(false);
    }
  };
  return <Modal size="m" label="Split into normal profiles" onClose={close}>
    <ModalTitle title={`Split ${name}`} onClose={close} />
    <ModalBody>
      {split?.list.length ? <div className="modal-pick">
        <div className="modal-pick-head">
          <button type="button" className="btn-ghost" onClick={() => setChecked(split.list.map(() => true))}>Select all</button>
          <button type="button" className="btn-ghost" onClick={() => setChecked(split.list.map(() => false))}>Clear</button>
        </div>
        <div className="modal-pick-list">{split.list.map((c, i) =>
          <label key={`${c.nozzle}|${c.layerHeight}`} className="modal-check"><Check checked={checked[i]} onCheckedChange={(next) => setChecked(checked.map((x, j) => (j === i ? next : x)))} />
            <span>{contextLabel(type, c)}</span></label>)}</div>
      </div> : <p>Not available for this printer</p>}
    </ModalBody>
    <ModalFooter lead={split?.list.length ? `${count} of ${split.list.length}` : undefined}>
      <button type="button" className="btn" onClick={close}>Cancel</button>
      {!!split?.list.length && <button type="button" className="btn-primary" disabled={!count || busy} onClick={() => void run()}>{count === 1 ? 'Add 1 profile' : `Add ${count} profiles`}</button>}
    </ModalFooter>
  </Modal>;
}
