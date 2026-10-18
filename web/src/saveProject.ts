import { jobRequest } from './actions';
import { getState, setState, toast } from './state/store';

let savingProject = false;

export async function saveProject(as = false) {
  const s = getState();
  if (savingProject || !s.api || !s.objects.length) return;
  if (s.api.mock) { toast('Saving a project requires the slicing service'); return; }
  let name = s.projectName ?? (s.objects[0].name.replace(/\.(stl|obj|3mf)$/i, '') + '.3mf');
  if (as) {
    const proposed = window.prompt('Save Project as', name);
    if (!proposed?.trim()) return;
    name = proposed.trim().replace(/\.3mf$/i, '') + '.3mf';
  }
  name = name.replace(/[<>:/\\|?*"]/g, '_');
  savingProject = true;
  toast('Saving project…');
  try {
    const request = await jobRequest();
    const id = await s.api.submit({ ...request, output: '3mf' });
    await new Promise<void>((resolve, reject) => {
      const unsubscribe = s.api!.events(id, {
        progress() {},
        done() { unsubscribe(); resolve(); },
        error(message) { unsubscribe(); reject(new Error(message)); },
      });
    });
    const response = await fetch(s.api.downloadUrl(id));
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    const href = URL.createObjectURL(await response.blob());
    const a = document.createElement('a');
    a.href = href; a.download = name; document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(href), 60000);
    setState({ projectName: name });
    toast('Project saved successfully');
  } catch (e) {
    toast(`Save failed: ${e instanceof Error ? e.message : e}`);
  } finally { savingProject = false; }
}
