import { toolArtifactManifest, toolFeatures } from '../api/client';
import { OPTIONS } from '../options';
import { effectiveConfig, resolvePreset, type State } from '../state/store';
import { deriveMethodAvailability } from './capabilities';


export function methodAvailability(state: State, printer = effectiveConfig(state, 'machine')) {
  const resolved = resolvePreset(state, 'machine', state.printer);
  return deriveMethodAvailability({printer:{known:!!resolved.base && !resolved.error,config:printer},engine:{features:toolFeatures,
    artifact_manifest:toolArtifactManifest,modeValues:toolFeatures.dump_options === true ? OPTIONS.nonplanar_mode?.enum_values : undefined}});
}
