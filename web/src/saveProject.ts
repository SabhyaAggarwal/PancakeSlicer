import { jobRequest } from './actions';
import { getState, setState, toast, type State } from './state/store';
import { promptDialog } from './components/Ask';
import { markSaved, snapshot } from './state/history';
import { output } from './flow/output';
import type { OperationTicket } from './flow/operation';
import { waitForJob } from './flow/job';
import { stampProjectProfiles } from './profiles/project';


export async function projectBlob(s: State, task?: OperationTicket): Promise<Blob> {
  const request = await jobRequest(s, true);
  task?.signal.throwIfAborted();
  task?.progress(0, 'uploading');
  const id = await s.api!.submit({ ...request, plate_index: undefined, output: '3mf' }, task?.signal,
    (fraction) => task?.progress(100 * fraction, 'uploading'));
  task?.progress(0, 'saving');
  await waitForJob(s.api!, id, task?.signal, (p) => task?.progress(p.pct, 'saving'));
  task?.signal.throwIfAborted();
  task?.progress(0, 'downloading');
  const response = await fetch(s.api!.downloadUrl(id), { signal: task?.signal });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return stampProjectProfiles(await response.blob(), s);
}

export async function saveProject(as = false) {
  const s = getState();
  if (!s.api || !s.objects.length && !s.projectPlates.some((p) => p.objects.length)) return;
  if (s.api.mock) { toast('Saving projects requires a connection to the slicing service.'); return; }
  const first = s.objects[0] ?? s.projectPlates.flatMap((p) => p.objects)[0];
  let name = s.projectName ?? (first.name.replace(/\.(stl|obj|3mf)$/i, '') + '.3mf');
  if (as) {
    const proposed = await promptDialog('Save Project as', name);
    if (!proposed) return;
    name = proposed.replace(/\.3mf$/i, '') + '.3mf';
  }
  name = name.replace(/[<>:/\\|?*"]/g, '_');
  const task = output.begin('preparing');
  try {
    const blob = await projectBlob(s, task);
    if (!task.current()) return;
    const href = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = href; a.download = name; document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(href), 60000);
    setState({ projectName: name });
    markSaved(snapshot(s));
    task.done();
    toast('Downloading project…');
  } catch (e) {
    if (!task.current()) return;
    task.fail(e);
    toast(`Oops! Couldn't save: ${e instanceof Error ? e.message : e}`);
  }
}
