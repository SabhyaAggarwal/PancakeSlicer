import type { ProfileOverlay, UserPreset } from '../api/types';
import { OPTIONS } from '../options';
import { mergeProfileAdoptionEntries, profileAdoptionValuesEqual, type ProfileAdoptionEntry, type ProfileAdoptionPlan } from './profile-adoption';
import type { ProfileLibraryAdoption, ProfileLibraryAdoptionReceipt, ProfileLibraryDocument } from './ownership';

interface SourceFile { path: string; size: number; sha256: string; mode: number; bytes: string }
interface ServiceSource {
  version: 1; complete: true; kind: 'service-user-presets'; origin: string; root: 'user-presets';
  directories: string[]; files: SourceFile[];
}
export interface ServiceProfileReview { path: string; sha256: string; profile: UserPreset }
export interface ServiceProfilePlanningDecision { path: string; sha256: string; choice: 'keep-current'; current: ProfileOverlay }
interface Capture { source: ServiceSource; profiles: ServiceProfileReview[] }
const object = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const encoder = new TextEncoder();
const hex = /^[a-f0-9]{64}$/;
const profileId = (id: unknown): id is string => typeof id === 'string' && id.startsWith('profile:') && id.length > 8
  && encoder.encode(id).length <= 200 && !/[\r\n\0]/.test(id);
const hash = async (bytes: Uint8Array<ArrayBuffer>) => [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n => n.toString(16).padStart(2,'0')).join('');
const validPath = (path: unknown): path is string => typeof path === 'string' && !/[\\\0]/.test(path)
  && (path === 'user-presets' || path.startsWith('user-presets/')) && path.split('/').every(part => !!part && part !== '.' && part !== '..');
function ordered(paths: string[]): boolean {
  return paths.every((path,i) => {
    if (!i) return true;
    const a=encoder.encode(paths[i-1]),b=encoder.encode(path),at=a.findIndex((v,j) => v !== b[j]);
    return at < 0 ? a.length < b.length : a[at] < b[at];
  });
}
function review(file: SourceFile, bytes: Uint8Array<ArrayBuffer>): ServiceProfileReview | undefined {
  if (!/\.json$/i.test(file.path)) return;
  let raw: unknown;
  try { raw=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes)); }
  catch { throw new Error(`${file.path}: the JSON bytes need explicit recovery. The original file is retained.`); }
  if (!object(raw)) return;
  if (!['name','type','id','overrides','base','overlay','inherits'].some(key => key in raw)) {
    if (Object.keys(raw).some(key => Object.hasOwn(OPTIONS,key))) throw new Error(`${file.path}: standalone settings need an explicit profile type, base and identity. The original bytes are retained.`);
    return;
  }
  if (typeof raw.name !== 'string' || !raw.name.trim() || raw.name !== raw.name.trim() || encoder.encode(raw.name).length > 200
    || /[\u0000-\u001f\u007f]/.test(raw.name) || !['machine','process','filament'].includes(String(raw.type)))
    throw new Error(`${file.path}: choose the profile name and type explicitly before recovery.`);
  if ('id' in raw && !profileId(raw.id)) throw new Error(`${file.path}: the stored identity needs explicit repair; it cannot be replaced automatically.`);
  if (raw.base !== undefined && (!object(raw.base) || typeof raw.base.name !== 'string' || !raw.base.name.trim()
    || raw.base.version !== undefined && typeof raw.base.version !== 'string')) throw new Error(`${file.path}: recover the retained base anchor before adoption.`);
  const base=object(raw.base) && typeof raw.base.name === 'string' && raw.base.name.trim() ? raw.base.name : undefined;
  const inherits=typeof raw.inherits === 'string' && raw.inherits.trim() ? raw.inherits : base;
  if (!inherits || !object(raw.overlay ?? raw.overrides) || raw.overlay !== undefined && !object(raw.overlay) || raw.overrides !== undefined && !object(raw.overrides)) throw new Error(`${file.path}: recover the retained base and settings before adoption.`);
  return {path:file.path,sha256:file.sha256,profile:{...raw,inherits,overrides:raw.overrides ?? raw.overlay} as UserPreset};
}
async function capture(adoption: ProfileLibraryAdoption): Promise<Capture> {
  if (adoption.scope !== 'profile-library-and-working-setup' || typeof adoption.source !== 'string' || !hex.test(adoption.digest)
    || await hash(encoder.encode(adoption.source)) !== adoption.digest) throw new Error('The service inventory exact source hash does not match. Keep it and reload the source.');
  const value: unknown=JSON.parse(adoption.source);
  if (!object(value) || value.version !== 1 || value.complete !== true || value.kind !== 'service-user-presets'
    || typeof value.origin !== 'string' || !/^service-data:[a-f0-9]{64}$/.test(value.origin) || value.root !== 'user-presets'
    || adoption.sourceId !== `service-profile-library:v1:${value.origin}` || !Array.isArray(value.directories) || !Array.isArray(value.files))
    throw new Error('The service inventory is incomplete or belongs to another source.');
  const source=value as unknown as ServiceSource,paths=new Set<string>(),profiles:ServiceProfileReview[]=[];
  if (!source.files.length || source.files.length+source.directories.length > 4096 || !source.directories.every(validPath)
    || !ordered(source.directories) || !source.directories.includes(source.root)) throw new Error('The complete service directory inventory needs recovery.');
  for (const path of source.directories) paths.add(path);
  if (paths.size !== source.directories.length) throw new Error('The service inventory has duplicate directories.');
  const directories=new Set(paths);
  const hasParent = (path: string) => path === source.root || directories.has(path.slice(0,path.lastIndexOf('/')));
  if (!source.directories.every(hasParent)) throw new Error('The service inventory omitted a parent directory.');
  let total=0;
  for (const file of source.files) {
    if (!object(file) || !validPath(file.path) || paths.has(file.path) || !hasParent(file.path)
      || !Number.isSafeInteger(file.size) || file.size < 0 || !Number.isInteger(file.mode) || file.mode < 0 || file.mode > 0xffffffff
      || typeof file.bytes !== 'string' || typeof file.sha256 !== 'string' || !hex.test(file.sha256) || (total+=file.size) > 2*1024*1024)
      throw new Error('The service file inventory is invalid or incomplete.');
    const binary=atob(file.bytes),bytes=Uint8Array.from(binary,c => c.charCodeAt(0));
    if (btoa(binary) !== file.bytes || bytes.length !== file.size || await hash(bytes) !== file.sha256) throw new Error(`${file.path}: the exact file bytes, size or hash do not match.`);
    paths.add(file.path);
    const profile=review(file,bytes);if (profile) profiles.push(profile);
  }
  if (!ordered(source.files.map(file => file.path))) throw new Error('The service file inventory is not in exact path order.');
  return {source,profiles};
}

export async function inspectServiceProfileAdoption(adoption: ProfileLibraryAdoption): Promise<{profiles:ServiceProfileReview[]}> {
  return {profiles:(await capture(adoption)).profiles};
}
function entries(captured: Capture, adoption: ProfileLibraryAdoption): ProfileAdoptionEntry[] {
  const resolutions=adoption.identityResolutions ?? [],byPath=new Map(captured.profiles.map(p => [p.path,p]));
  const mapped=new Map<string,string>(),ids=new Set<string>();
  if (!Array.isArray(resolutions)) throw new Error('The explicit identity resolutions need repair.');
  for (const resolution of resolutions) {
    const file=object(resolution) && byPath.get(resolution.path);
    if (!file || file.profile.id !== undefined || resolution.sha256 !== file.sha256 || !profileId(resolution.profileId)
      || mapped.has(resolution.path) || ids.has(resolution.profileId)) throw new Error('An identity resolution targets another file, existing identity, duplicate target or changed bytes.');
    mapped.set(resolution.path,resolution.profileId);ids.add(resolution.profileId);
  }
  ids.clear();
  return captured.profiles.map(file => {
    const id=file.profile.id ?? mapped.get(file.path);
    if (!id) throw new Error(`${file.path}: ${file.profile.name} (${file.profile.type}) is a supported older profile. Choose an existing same-type identity or explicitly create a new identity to recover it; no identity has been guessed.`);
    if (ids.has(id)) throw new Error(`${file.path}: more than one retained profile targets ${id}. Resolve each identity explicitly.`);
    ids.add(id);
    const base=file.profile.base ?? {name:file.profile.inherits,version:'legacy:unknown'};
    return {path:`service-user-presets/profiles/${id}`,kind:'profile',key:id,value:{...file.profile,id,base:base.version ? base : {...base,version:'legacy:unknown'}}};
  });
}

export async function planServiceProfileAdoption(remote: ProfileLibraryDocument, adoption: ProfileLibraryAdoption, receipts: readonly ProfileLibraryAdoptionReceipt[], decisions: readonly ServiceProfilePlanningDecision[] = []): Promise<ProfileAdoptionPlan> {
  const conflicts:string[]=[],operationId=`service-adoption:${adoption.digest}`;
  const result=(document=remote,alreadyAcknowledged=false,accepted=adoption):ProfileAdoptionPlan => ({document,conflicts,adoption:accepted,operationId,alreadyAcknowledged});
  try {
    const current=await capture(adoption),baselines=new Map<string,{entry:ProfileAdoptionEntry;revision:number}[]>();
    const relevant=receipts.filter(receipt => receipt.sourceId === adoption.sourceId);
    const acknowledged=async (receipt:ProfileLibraryAdoptionReceipt) => {
      if (receipt.durable !== true || !Number.isInteger(receipt.revision) || receipt.revision < 0 || receipt.operationId !== `service-adoption:${receipt.digest}`) throw new Error('The service identity adoption receipt needs recovery.');
      return entries(await capture(receipt),receipt);
    };
    if (new Set(relevant.map(receipt => receipt.digest)).size !== relevant.length) throw new Error('The service returned duplicate adoption receipts.');
    const known=relevant.find(receipt => receipt.digest === adoption.digest);
    if (known) {
      await acknowledged(known);
      if (known.source !== adoption.source) throw new Error('An acknowledged digest has different source bytes.');
      return result(remote,true,{scope:known.scope,sourceId:known.sourceId,digest:known.digest,source:known.source,...(known.identityResolutions ? {identityResolutions:known.identityResolutions} : {})});
    }
    const after=entries(current,adoption),currentPaths=new Map(current.profiles.map(file => [file.path,file]));
    const choices=new Map(adoption.identityResolutions?.map(resolution => [resolution.path,resolution.profileId]));
    for (const receipt of relevant) {
      for (const entry of await acknowledged(receipt)) { const prior=baselines.get(entry.path) ?? [];prior.push({entry,revision:receipt.revision});baselines.set(entry.path,prior); }
      for (const mapping of receipt.identityResolutions ?? []) {
        const file=currentPaths.get(mapping.path);
        if (file && mapping.sha256 === file.sha256 && file.profile.id === undefined && choices.get(file.path) !== mapping.profileId)
          throw new Error(`${file.path}: retain the acknowledged identity ${mapping.profileId} or recover an explicit copy separately.`);
      }
    }
    const saved=new Map(remote.profiles.map(profile => [profile.id,profile]));
    for (const entry of after) {
      const existing=saved.get(entry.key);
      if (existing && existing.type !== (entry.value as UserPreset).type) throw new Error(`${entry.key}: the selected recovery identity has another profile type.`);
    }
    const keep=new Set<string>();
    if (!Array.isArray(decisions)) throw new Error('Review the per-file Keep Current choices before adoption.');
    for (const decision of decisions) {
      const file=object(decision) && typeof decision.path === 'string' && currentPaths.get(decision.path),id=file && file.profile.id,actual=id && saved.get(id);
      if (!file || decision.choice !== 'keep-current' || decision.sha256 !== file.sha256 || !id || !actual
        || !object(decision.current) || decision.current.id !== id || decision.current.type !== file.profile.type
        || actual.type !== file.profile.type || keep.has(id) || !profileAdoptionValuesEqual(decision.current,actual))
        throw new Error('A Keep Current choice is stale, duplicated or belongs to another source or identity. Review the exact file and saved record again.');
      keep.add(id);
    }
    return result(mergeProfileAdoptionEntries(remote,after.filter(entry => !keep.has(entry.key)),baselines,conflicts));
  } catch (error) { conflicts.push(error instanceof Error ? error.message : String(error));return result(); }
}
