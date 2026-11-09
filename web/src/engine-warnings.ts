export const bareName = (name: string) => name.replace(/\.(stl|obj|3mf|amf|step|stp|drc|ply|off)$/i, '');

const n = (s: string) => Number(s).toLocaleString('en-US');
const KNOWN: [RegExp, ((m: RegExpMatchArray) => string) | null][] = [
  [/^Curved tops returned the head-strike neighbourhood to planar layers/i, () => 'Some curved areas were kept flat so the print head stays clear'],
  [/^Curved tops printed planar: every curved surface was struck/i, () => 'Printed with flat layers because the print head would bump into every curved top'],
  [/^curved top \d+ prints planar to keep firmware time within (\d+)%/i, (m) => `Some top surfaces stayed flat to keep print time within ${m[1]}%`],
  [/^trying native curved roads before dropping a top/i, null],
  [/^Conformal layers would bump the print head in (\d+) moves; sliced again with unrounded shells/i, () => 'Adjusted curved shells so the print head stays clear'],
  [/^Conformal layers would bump the print head in (\d+) moves.*printed with flat layers/i, (m) => `Printed with flat layers so the print head doesn't bump the part (${n(m[1])} potential collisions avoided)`],
  [/^no nonflat top surface meets/i, null],
  [/^Smooth morph would make the print head bump the part in \d+ moves; printed flat/i, null],
  [/^Experimental: cooling duct not checked\./i, null],
  [/^mesh repaired on load/i, () => 'Auto-repaired mesh on import'],
  [/^Clearance repair passes: \d+\..*Curved moves that still hit printed material: (\d+)/i, (m) => `${n(m[1])} curved moves still brush the printed part`],
  [/^Looks like .* has floating regions/i, () => 'Floating overhangs detected—try rotating the model or turning on supports'],
];

export interface PlainWarning {
  object?: string;
  text?: string | null;
  raw: string;
}

export function plainWarnings(warnings: string[], objectNames: string[]): PlainWarning[] {
  const names = objectNames.map(bareName).filter(Boolean).sort((a, b) => b.length - a.length);
  const seen = new Set<string>();
  const out: PlainWarning[] = [];
  for (const raw of warnings) {
    const w = raw.trim();
    const prefix = /^([^:\n]+?):\s+([\s\S]+)$/.exec(w);
    const named = prefix && names.find((name) => bareName(prefix[1].trim()) === name);
    const object = named ?? names.find((name) => new RegExp(`\\b${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}(\\.\\w+)?\\b`).test(w));
    const body = named ? prefix![2] : w;
    const hit = KNOWN.find(([re]) => re.test(body));
    const text = hit ? (hit[1] ? hit[1](body.match(hit[0])!) : null) : undefined;
    const key = `${object ?? ''}|${text ?? w}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ ...(object ? { object } : {}), text, raw: named ? body : w });
  }
  return out;
}
