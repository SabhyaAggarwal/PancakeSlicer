export function unsnappy(input: Buffer, limit: number): Buffer {
  let at = 0, size = 0, shift = 0;
  for (;;) {
    if (at >= input.length || shift > 28) throw new Error('Invalid Snappy length.');
    const byte = input[at++];
    size += (byte & 127) * 2 ** shift;
    if (!(byte & 128)) {
      if (shift > 0 && byte === 0) throw new Error('Noncanonical Snappy length.');
      break;
    }
    shift += 7;
  }
  if (!Number.isSafeInteger(size) || size > limit) throw new Error('Snappy budget exceeded.');
  const output = Buffer.alloc(size);
  let written = 0;
  while (at < input.length) {
    const tag = input[at++], kind = tag & 3;
    let length: number, offset = 0;
    if (!kind) {
      length = tag >>> 2;
      if (length < 60) length++;
      else {
        const count = length - 59;
        if (at + count > input.length) throw new Error('Short Snappy literal.');
        length = 1;
        for (let i = 0; i < count; i++) length += input[at++] * 2 ** (8 * i);
      }
      if (at + length > input.length || written + length > size) throw new Error('Invalid Snappy literal.');
      input.copy(output, written, at, at + length);
      at += length;
    } else {
      const count = kind === 1 ? 1 : kind === 2 ? 2 : 4;
      if (at + count > input.length) throw new Error('Short Snappy copy.');
      length = kind === 1 ? 4 + ((tag >>> 2) & 7) : 1 + (tag >>> 2);
      if (kind === 1) offset = (tag & 224) * 8 + input[at++];
      else for (let i = 0; i < count; i++) offset += input[at++] * 2 ** (8 * i);
      if (offset < 1 || offset > written || written + length > size) throw new Error('Invalid Snappy copy.');
      for (let i = 0; i < length; i++) output[written + i] = output[written + i - offset];
    }
    written += length;
  }
  if (written !== size) throw new Error('Incomplete Snappy output.');
  return output;
}
