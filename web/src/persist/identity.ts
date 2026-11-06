
const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
let lastTime = -1;
const entropy = new Uint8Array(10);
export function projectId() {
  const now = Math.max(Date.now(), lastTime);
  if (now === lastTime) {
    for (let i = entropy.length - 1; i >= 0; i--) if ((entropy[i] = (entropy[i] + 1) & 255) !== 0) break;
  } else crypto.getRandomValues(entropy);
  lastTime = now;
  let time = now, id = '';
  for (let i = 0; i < 10; i++) { id = ALPHABET[time % 32] + id; time = Math.floor(time / 32); }
  let bits = 0, value = 0;
  for (const byte of entropy) {
    value = (value << 8) | byte; bits += 8;
    while (bits >= 5) { bits -= 5; id += ALPHABET[(value >>> bits) & 31]; }
  }
  return id;
}
export const isProjectId = (id: string) => /^[0-7][0-9A-HJKMNP-TV-Z]{25}$/.test(id);
