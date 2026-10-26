import { jobRequest } from './actions';
import { getState, setState, toast, type State } from './state/store';
import { promptDialog } from './components/Ask';
import { markSaved, snapshot } from './state/history';

let savingProject = false;

export async function projectBlob(s: State): Promise<Blob> {
  const request = await jobRequest(s, true);
  const id = await s.api!.submit({ ...request, plate_index: undefined, output: '3mf' });
  await new Promise<void>((resolve, reject) => {
    const unsubscribe = s.api!.events(id, {
      progress() {},
      done() { unsubscribe(); resolve(); },
      error(message) { unsubscribe(); reject(new Error(message)); },
    });
  });
  const response = await fetch(s.api!.downloadUrl(id));
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.blob();
}

export async function saveProject(as = false) {
  const s = getState();
  if (savingProject || !s.api || !s.objects.length && !s.projectPlates.some((p) => p.objects.length)) return;
  if (s.api.mock) { toast('Saving a project requires the slicing service'); return; }
  let name = s.projectName ?? (s.objects[0].name.replace(/\.(stl|obj|3mf)$/i, '') + '.3mf');
  if (as) {
    const proposed = await promptDialog('Save Project as', name);
    if (!proposed) return;
    name = proposed.replace(/\.3mf$/i, '') + '.3mf';
  }
  name = name.replace(/[<>:/\\|?*"]/g, '_');
  savingProject = true;
  toast('Saving project…');
  try {
    const href = URL.createObjectURL(await projectBlob(s));
    const a = document.createElement('a');
    a.href = href; a.download = name; document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(href), 60000);
    setState({ projectName: name });
    markSaved(snapshot(s));
    toast('Project saved successfully');
  } catch (e) {
    toast(`Save failed: ${e instanceof Error ? e.message : e}`);
  } finally { savingProject = false; }
}
