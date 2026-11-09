export const MODE_LABELS: Record<string, string> = {
  off: 'Flat layers', auto: 'Auto', manual: 'Manual curved settings', oneshot: 'One-shot field', nonplanar_top: 'Smoothest tops', curved_tops: 'Fast curved tops', smooth_morph: 'Smooth morph', slant: 'Slant (experimental)',
  conformal_layers: 'Conformal layers (experimental)',
};
export const MODE_HELP: Record<string, string> = {
  off: 'Flat layers: every layer flat. Fastest.',
  auto: 'Auto: the slicer compares the methods for each part and prints the one it measures best.',
  curved_tops: 'Fast curved tops: bends the outermost top layer where it adds no time. Other tops stay flat.',
  nonplanar_top: 'Smoothest tops: bends the top three layers over every gentle top. Smoothest finish, 10–50 % slower.',
  smooth_morph: 'Smooth morph: bends every layer from the bed up. Best for domes and lids, up to 1.4× slower.',
  conformal_layers: 'Conformal layers (experimental): the top shells follow each gentle top (up to 20°) at one thickness, so a top prints as one unbroken layer; walls and fill stay Orca\'s. 1.1–1.4× flat time.',
  slant: 'Slant (experimental): a flat first layer, then each layer tilts a little more, as a belt printer\'s do, up to the head\'s limit. Smoother on faces sloping with it; flat tops step.',
};
