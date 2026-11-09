export const MODE_LABELS: Record<string, string> = {
  off: 'Flat layers', auto: 'Auto', manual: 'Manual curved settings', oneshot: 'One-shot field', nonplanar_top: 'Curved tops', smooth_morph: 'Smooth morph', slant: 'Slant',
  conformal_layers: 'Conformal layers',
};
export const MODE_HELP: Record<string, string> = {
  off: 'Flat layers: every layer flat. Fastest.',
  auto: 'Auto: the slicer compares the methods for each part and prints the one it measures best.',
  nonplanar_top: 'Curved tops: bends the top layers over every gentle top the head clears (3 by default).',
  smooth_morph: 'Smooth morph: bends every layer from the bed up. Best for domes and lids, up to 1.4× slower.',
  conformal_layers: 'Conformal layers (experimental): the top shells follow each gentle top at one thickness, as one unbroken layer.',
  slant: 'Slant (experimental): a flat first layer, then each layer tilts a little more, up to the head\'s limit.',
};
