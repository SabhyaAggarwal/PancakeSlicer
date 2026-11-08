export const bareName = (name: string) => name.replace(/\.(stl|obj|3mf|amf|step|stp|drc|ply|off)$/i, '');

const n = (s: string) => Number(s).toLocaleString('en-US');
const KNOWN: [RegExp, ((m: RegExpMatchArray) => string) | null][] = [
  [/^Curved tops returned the head-strike neighbourhood to planar layers/i, () => 'Some curved areas printed flat to keep the head clear'],
  [/^Curved tops printed planar: every curved surface was struck/i, () => 'Curved tops printed flat: the head would hit every one'],
  [/^curved top \d+ prints planar to keep firmware time within (\d+)%/i, (m) => `A curved top printed flat to keep print time within ${m[1]}%`],
  [/^trying native curved roads before dropping a top/i, null],
  [/^Conformal layers met the print head in (\d+) moves; sliced again with unrounded shells/i, () => 'Sliced again with sharper shells to keep the head clear'],
  [/^Conformal layers met the print head in (\d+) moves.*printed with flat layers/i, (m) => `Printed flat: the print head would hit the part in ${n(m[1])} moves`],
  [/^no nonflat top surface meets/i, null],
  [/^Smooth morph would hit the part in \d+ moves; printed flat/i, null],
  [/^Experimental: cooling duct not checked\. (\d+) moves would graze/i, (m) => `Cooling duct not checked: ${n(m[1])} moves may brush the fan duct`],
  [/^mesh repaired on load/i, () => 'Model repaired on load'],
  [/^Clearance repair passes: \d+\..*Curved moves that still hit printed material: (\d+)/i, (m) => `${n(m[1])} curved moves still brush the printed part`],
  [/^Looks like .* has floating regions/i, () => 'Floating regions: rotate it or turn on supports'],
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
