import { useEffect, useState } from 'react';
import { Modal, ModalTitle } from './Modal';

export type BuildAbout = { version: string; revision: string; image: string; license: string; source_url: string; source_configured: boolean; source_sha256: string };

export function AboutContent({ about }: { about: BuildAbout }) {
  return <>
    <h2>Nonplanar OrcaSlicer</h2>
    <p>Version {about.version}</p>
    <p>Licence: <a href="https://www.gnu.org/licenses/agpl-3.0.html" target="_blank" rel="noreferrer">{about.license}</a></p>
    {about.source_configured ? <>
    <p>This program is free software. You can receive the Corresponding Source of this running version at no charge:</p>
    <p><a href={about.source_url} target="_blank" rel="noreferrer">Get source code for this version</a></p>
    </> : <p role="alert">The operator must configure the Corresponding Source offer required by AGPL-3.0 §13.</p>}
    <p><a href="/third-party-licences.txt" target="_blank" rel="noreferrer">Third-party licences</a></p>
    <p className="about-revision">Revision: {about.revision}{about.image && <><br />Image: {about.image}</>}</p>
  </>;
}

export default function About({ close }: { close: () => void }) {
  const [about, setAbout] = useState<BuildAbout | null>(null);
  const [error, setError] = useState('');
  const [attempt, setAttempt] = useState(0);
  useEffect(() => {
    const controller = new AbortController();
    setError('');
    fetch('/api/about', { signal: controller.signal }).then(async (response) => {
      if (!response.ok) throw new Error(`Server returned ${response.status}`);
      setAbout(await response.json() as BuildAbout);
    }).catch((e: unknown) => {
      if (!controller.signal.aborted) setError(e instanceof Error ? e.message : 'Service unavailable');
    });
    return () => controller.abort();
  }, [attempt]);
  return <Modal className="about-dialog" label="About OrcaSlicer" onClose={close}>
    <ModalTitle title="About OrcaSlicer" onClose={close} />
    <div className="about-body">
      {about ? <AboutContent about={about} /> : error ? <p role="alert">Unable to load source information: {error}. <button className="side-btn" onClick={() => setAttempt(attempt + 1)}>Retry</button></p> : <p role="status">Loading version and source information…</p>}
    </div>
    <div className="bed-dialog-buttons"><button className="side-btn" data-autofocus onClick={close}>OK</button></div>
  </Modal>;
}
