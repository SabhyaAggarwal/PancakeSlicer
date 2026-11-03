import { mutation, responseError } from '../api/access';
import type { ProfileLibraryAdoptionReceipt, ProfileLibraryDocument, ProfileLibraryWrite, ProfileLibraryWriteResult } from '../profiles/ownership';
const validate = (doc: ProfileLibraryDocument) => {
  if (!doc || doc.schema !== 1 || !Number.isInteger(doc.revision) || doc.revision < 0 || !Array.isArray(doc.profiles)
    || doc.printers?.schema !== 1 || !doc.printers.setups) throw new Error('The profile library is invalid. Export your profiles before repairing storage.');
  return doc;
};
export async function readProfileLibrary(_mock=false):Promise<ProfileLibraryDocument> {
  const response=await fetch('/api/profile-library',{cache:'no-store'});
  if(response.status === 404)throw Object.assign(new Error('The required profile library API is unavailable. Your profile draft is kept. Connect the updated service and retry.'),{code:'profile_api_required',recovery:'update_service'});
  if(!response.ok)throw await responseError(response);
  return validate(await response.json());
}
export async function persistProfileLibrary(write: ProfileLibraryWrite, _mock = false): Promise<ProfileLibraryWriteResult> {
  validate(write.document);
  const response = await mutation('/api/profile-library',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(write)});
  if (!response.ok) throw await responseError(response);
  return response.json();
}
function receipt(value: ProfileLibraryAdoptionReceipt): ProfileLibraryAdoptionReceipt {
  if(value?.durable!==true || value.scope!=='profile-library-and-working-setup' || !/^[a-f0-9]{64}$/.test(value.digest) || typeof value.source!=='string' || typeof value.sourceId!=='string' || typeof value.operationId!=='string' || !Number.isInteger(value.revision))throw new Error('The service returned an invalid profile adoption receipt. Keep browser data and retry.');
  return value;
}
export async function readProfileLibraryAdoptions():Promise<ProfileLibraryAdoptionReceipt[]> {
  const response=await fetch('/api/profile-library/adoptions',{cache:'no-store'});
  if(!response.ok)throw await responseError(response);
  const values=await response.json();if(!Array.isArray(values))throw new Error('Profile adoption receipts could not be read.');
  return values.map(receipt);
}
export async function readProfileLibraryAdoption(digest:string):Promise<ProfileLibraryAdoptionReceipt|undefined> {
  const response=await fetch(`/api/profile-library/adoptions/${encodeURIComponent(digest)}`,{cache:'no-store'});
  if(!response.ok){const error=await responseError(response);if(error.status===404 && error.code==='adoption_missing')return undefined;throw error;}
  return receipt(await response.json());
}
