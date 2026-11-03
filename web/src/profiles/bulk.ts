import { selectedVisible, type ProfileKey, type ProfileSelection } from './selection';

export interface ProfileReference { readonly kind: 'active' | 'default' | 'project' | 'inheritance'; readonly label: string; readonly owner?: ProfileKey }
export interface ProfileAccess {
  label(key: ProfileKey): string;
  isInUse(key: ProfileKey): readonly ProfileReference[];
  isDeletable(key: ProfileKey): boolean;
  readonly referencesComplete: boolean;
}
export interface NamedProfile { readonly key: ProfileKey; readonly name: string }
export interface BlockedProfile extends NamedProfile { readonly reason: string; readonly references: readonly ProfileReference[] }
export interface DeletePlan {
  readonly scope: string;
  readonly selected: readonly NamedProfile[];
  readonly deletable: readonly NamedProfile[];
  readonly blocked: readonly BlockedProfile[];
  readonly confirmation: {
    readonly selectedCount: number; readonly deleteCount: number; readonly blockedCount: number;
    readonly deleteNames: readonly string[]; readonly blocked: readonly BlockedProfile[];
  };
}
export function planDeleteSelected(selection: ProfileSelection, access: ProfileAccess): DeletePlan {
  const selected = selectedVisible(selection).map((key) => ({ key, name: access.label(key) }));
  const deletable: NamedProfile[] = [], blocked: BlockedProfile[] = [];
  for (const profile of selected) {
    const references = access.isInUse(profile.key).map((r) => ({ ...r }));
    const reason = !access.isDeletable(profile.key) ? 'This profile is read-only.'
      : !access.referencesComplete ? 'Profile references are still loading.'
      : references.length ? `In use: ${references.map((r) => r.label).join('; ')}` : null;
    if (reason) blocked.push({ ...profile, reason, references }); else deletable.push(profile);
  }
  return { scope: selection.scope, selected, deletable, blocked,
    confirmation: { selectedCount: selected.length, deleteCount: deletable.length, blockedCount: blocked.length,
      deleteNames: deletable.map((p) => p.name), blocked } };
}
export function revalidateDeletePlan(plan: DeletePlan, current: ProfileSelection, access: ProfileAccess): DeletePlan {
  const confirmed = new Set(plan.deletable.map((p) => p.key));
  const visible = plan.scope === current.scope ? selectedVisible(current).filter((key) => confirmed.has(key)) : [];
  const narrowed: ProfileSelection = visible.length
    ? { ...current, value: 'selected', selected: [visible[0], ...visible.slice(1)] }
    : { ...current, value: 'empty', selected: [] };
  return planDeleteSelected(narrowed, access);
}
export type DeleteOutcome = { readonly key: ProfileKey; readonly status: 'deleted' }
  | { readonly key: ProfileKey; readonly status: 'failed' | 'unknown'; readonly error: string };
export interface DeleteResult {
  readonly state: 'empty' | 'blocked' | 'complete' | 'partial' | 'failed';
  readonly deleted: readonly ProfileKey[];
  readonly failed: readonly { key: ProfileKey; error: string }[];
  readonly unknown: readonly { key: ProfileKey; error: string }[];
  readonly notAttempted: readonly ProfileKey[];
  readonly blocked: readonly BlockedProfile[];
}
export function deleteResult(plan: DeletePlan, outcomes: readonly DeleteOutcome[]): DeleteResult {
  const byKey = new Map<ProfileKey, DeleteOutcome[]>();
  for (const outcome of outcomes) byKey.set(outcome.key, [...byKey.get(outcome.key) ?? [], outcome]);
  const deleted: ProfileKey[] = [], failed: { key: ProfileKey; error: string }[] = [], unknown: { key: ProfileKey; error: string }[] = [], notAttempted: ProfileKey[] = [];
  for (const { key } of plan.deletable) {
    const records = byKey.get(key) ?? [], result = records[0];
    if (!result) { notAttempted.push(key); continue; }
    if (records.some((r) => r.status !== result.status)) { unknown.push({ key, error: 'Conflicting delete responses. Refresh to check this profile.' }); continue; }
    switch (result.status) {
      case 'deleted': deleted.push(key); break;
      case 'failed': failed.push({ key, error: result.error }); break;
      case 'unknown': unknown.push({ key, error: result.error }); break;
      default: { const never: never = result; return never; }
    }
  }
  const state = !plan.selected.length ? 'empty' : !plan.deletable.length ? 'blocked'
    : deleted.length === plan.deletable.length && !plan.blocked.length ? 'complete'
    : failed.length === plan.deletable.length && !plan.blocked.length ? 'failed' : 'partial';
  return { state, deleted, failed, unknown, notAttempted, blocked: plan.blocked };
}
export function applyDeleteResult<T>(current: readonly T[], result: DeleteResult, keyOf: (profile: T) => ProfileKey): T[] {
  const removed = new Set(result.deleted);
  return current.filter((profile) => !removed.has(keyOf(profile)));
}

export interface ExportedProfile extends NamedProfile { readonly filename: string; readonly text: string }
export interface ExportResult {
  readonly state: 'empty' | 'complete' | 'partial' | 'failed';
  readonly files: readonly ExportedProfile[];
  readonly failed: readonly (NamedProfile & { error: string })[];
}
export function serializeSelected(selection: ProfileSelection, access: {
  label(key: ProfileKey): string; serialize(key: ProfileKey): string;
}): ExportResult {
  const keys = selectedVisible(selection), files: ExportedProfile[] = [], failed: (NamedProfile & { error: string })[] = [];
  const used = new Set<string>();
  for (const key of keys) {
    const name = access.label(key);
    try {
      const text = access.serialize(key);
      const stem = name.replace(/[<>:"/\\|?*\u0000-\u001f]/g, '_').replace(/[. ]+$/g, '').slice(0, 120) || 'profile';
      let filename = `${stem}.json`, n = 2;
      while (used.has(filename.toLowerCase())) filename = `${stem} (${n++}).json`;
      used.add(filename.toLowerCase()); files.push({ key, name, filename, text });
    } catch (e) { failed.push({ key, name, error: e instanceof Error ? e.message : String(e) }); }
  }
  return { state: !keys.length ? 'empty' : !failed.length ? 'complete' : files.length ? 'partial' : 'failed', files, failed };
}
export type ActionAvailability = { readonly enabled: true } | { readonly enabled: false; readonly reason: string };
const disabled = (reason: string): ActionAvailability => ({ enabled: false, reason });
export function selectedActions(selection: ProfileSelection, support: {
  canUse(key: ProfileKey): ActionAvailability;
  readonly compareCount: 1 | 2;
  compareReason(keys: readonly ProfileKey[]): string | undefined;
  canReset?(key: ProfileKey): ActionAvailability;
}): { use: ActionAvailability; compare: ActionAvailability; export: ActionAvailability; reset: ActionAvailability } {
  const keys = selectedVisible(selection);
  const compareReason = keys.length === support.compareCount ? support.compareReason(keys) : `Select exactly ${support.compareCount} profile${support.compareCount === 1 ? '' : 's'} to compare.`;
  return {
    use: keys.length === 1 ? support.canUse(keys[0]) : disabled('Select exactly one profile to use.'),
    compare: compareReason === undefined ? { enabled: true } : disabled(compareReason),
    export: keys.length ? { enabled: true } : disabled('Select profiles to export.'),
    reset: keys.length === 1 && support.canReset ? support.canReset(keys[0])
      : disabled(keys.length > 1 ? 'Reset profiles individually to review inheritance and project edits.' : 'Select one profile with an available reset action.'),
  };
}
