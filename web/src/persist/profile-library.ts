import { mutation, responseError } from '../api/access';
import type { ProfileLibraryDocument, ProfileLibraryWrite, ProfileLibraryWriteResult } from '../profiles/ownership';
const validate = (doc: ProfileLibraryDocument) => {
  if (!doc || doc.schema !== 1 || !Number.isInteger(doc.revision) || doc.revision < 0 || !Array.isArray(doc.profiles)
    || doc.printers?.schema !== 1 || !doc.printers.setups || doc.epoch !== undefined && typeof doc.epoch !== 'string') throw new Error('The profile library is invalid.');
  return doc;
};
export async function readProfileLibrary(_mock=false):Promise<ProfileLibraryDocument> {
  const response=await fetch('/api/profile-library',{cache:'no-cache'});
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
