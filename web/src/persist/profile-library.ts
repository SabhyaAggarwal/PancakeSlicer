import { mutation, responseError } from '../api/access';
import type { ProfileLibraryDocument, ProfileLibraryWrite, ProfileLibraryWriteResult } from '../profiles/ownership';
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
