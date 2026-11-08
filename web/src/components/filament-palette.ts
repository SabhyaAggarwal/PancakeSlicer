export const FILAMENT_PALETTE: readonly { hex: string; name: string }[] = [
  { hex: '#FFFFFF', name: 'Jade White' }, { hex: '#A6A9AA', name: 'Silver' }, { hex: '#8E9089', name: 'Gray' }, { hex: '#000000', name: 'Black' },
  { hex: '#C12E1F', name: 'Red' }, { hex: '#FF6A13', name: 'Orange' }, { hex: '#FEC600', name: 'Sunflower Yellow' }, { hex: '#F4EE2A', name: 'Yellow' },
  { hex: '#00AE42', name: 'Bambu Green' }, { hex: '#00B1B7', name: 'Turquoise' }, { hex: '#0086D6', name: 'Cyan' }, { hex: '#0056B8', name: 'Cobalt Blue' },
  { hex: '#5E43B7', name: 'Purple' }, { hex: '#EC008C', name: 'Magenta' }, { hex: '#9D432C', name: 'Brown' }, { hex: '#F7E6DE', name: 'Beige' },
];
export const paletteName = (hex: string) => FILAMENT_PALETTE.find((c) => c.hex === hex.toUpperCase())?.name;
