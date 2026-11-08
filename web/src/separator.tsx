export const SEP = ' | ';

export const joinParts = (parts: readonly (string | false | null | undefined | 0)[]): string => parts.filter(Boolean).join(SEP);

export function Sep() {
  return <span className="sep"> | </span>;
}
