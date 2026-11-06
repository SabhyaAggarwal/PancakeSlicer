import { loadCatalog } from '../actions';
import { useStore } from '../state/store';


export function ProfileLibraryNotice() {
  const load = useStore(s => s.profileLibraryLoad);
  if (load.state !== 'failed') return null;
  return <div className="profile-library-notice" role="alert">
    <div><strong>Profiles didn't load</strong><button type="button" className="btn" onClick={() => void loadCatalog()}>Retry</button></div>
    <span>{load.error}</span>
  </div>;
}
