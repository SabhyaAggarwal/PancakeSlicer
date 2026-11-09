export interface TourPage { title: string; points: string[]; shots: [string, string][] }
export const TOUR: TourPage[] = [
  { title: 'One profile that adapts to your hardware', points: [
    'PancakeSlicer uses dynamic profiles. In traditional slicers, you have to juggle separate print profiles for every nozzle size and layer height, and over time your tweaks get out of sync.',
    'Here, a profile like Everyday focuses only on how you want your part built: wall count, infill, supports, and surface finish. Everything tied to your hardware is calculated automatically—line widths scale with your nozzle, shell counts adjust to your layer height, and speeds match your printer.',
    'That means the same Everyday profile works effortlessly across any nozzle, layer height, or printer! Auto-calculated settings are marked with ✦, and you can override any value whenever you like. Classic fixed profiles are right here too if you prefer them.'],
    shots: [['/tour/dynamic.webp', 'Everyday: automatically tuned for your nozzle and layer height']] },
  { title: 'Curved tops', points: [
    'Traditional slicers print strictly flat horizontal layers, leaving visible stair-steps on gentle slopes and domes.',
    'PancakeSlicer can smoothly curve the top layers to follow the contours of your model—right on a standard 3-axis printer.',
    'Get smooth, clean top surfaces without slowing down the whole print with ultra-thin layers.'],
    shots: [['/tour/roof-flat.webp', 'Flat layers | 0.20 mm'], ['/tour/roof-curved.webp', 'Curved tops | 0.20 mm']] },
  { title: 'Your workflow', points: [
    'Your projects, profiles, and printers stay synced on your server, ready whenever you open a browser.',
    'Slice from your desktop, laptop, tablet, or phone and send prints straight to your printer over Wi-Fi.'],
    shots: [['/tour/workflow.webp', 'All your projects, on any device']] },
];
