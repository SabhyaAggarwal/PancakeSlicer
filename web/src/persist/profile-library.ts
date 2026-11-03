import { mutation, responseError } from '../api/access';
import type { ProfileLibraryDocument, ProfileLibraryWrite, ProfileLibraryWriteResult } from '../profiles/ownership';
const validate = (doc: ProfileLibraryDocument) => {
  if (!doc || doc.schema !== 1 || !Number.isInteger(doc.revision) || doc.revision < 0 || !Array.isArray(doc.profiles)
    || doc.printers?.schema !== 1 || !doc.printers.setups || doc.epoch !== undefined && typeof doc.epoch !== 'string') throw new Error('The profile library is invalid.');
  return doc;
};
export async function readProfileLibrary(_mock=false):Promise<ProfileLibraryDocument> {
  const response=await fetch('/api/profile-library',{cache:'no-store'});
  if(!response.ok){
    const error=await responseError(response);
    if(error.code==='legacy_profiles_pending')error.message='The profile service is out of date. Update and restart it, then reload.';
    throw error;
  }
  return validate(await response.json());
}
export async function persistProfileLibrary(write: ProfileLibraryWrite, _mock = false): Promise<ProfileLibraryWriteResult> {
  validate(write.document);
  const response = await mutation('/api/profile-library',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(write)});
  if (!response.ok) throw await responseError(response);
  return response.json();
}
export interface ProfileRecoveryCount { count: number; unreadable: number }
export async function readProfileRecovery(): Promise<ProfileRecoveryCount> {
  const response=await fetch('/api/profile-library/recovery',{cache:'no-store'});
  if(!response.ok)throw await responseError(response);
  const value=await response.json() as Partial<ProfileRecoveryCount>;
  if(!Number.isInteger(value?.count) || value.count!<0 || !Number.isInteger(value.unreadable) || value.unreadable!<0)throw new Error('The profile recovery count is invalid.');
  return value as ProfileRecoveryCount;
}
export interface ProfileRecoveryWrite { operationId: string; expectedRevision: number; action: 'save' | 'reset' }
export interface ProfileRecoveryResult extends ProfileRecoveryWrite { revision: number; saved: number; unreadable: number }
export async function commitProfileRecovery(write: ProfileRecoveryWrite): Promise<ProfileRecoveryResult> {
  const response=await mutation('/api/profile-library/recovery',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(write)});
  if(!response.ok)throw await responseError(response);
  const value=await response.json() as ProfileRecoveryResult;
  if(value?.operationId!==write.operationId || value.action!==write.action || !Number.isInteger(value.revision) || value.revision<write.expectedRevision)
    throw new Error('The service returned an invalid profile acknowledgement.');
  return value;
}
