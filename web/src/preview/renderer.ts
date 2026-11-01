import * as THREE from 'three';
import { TEX_WIDTH, type PreviewData } from './data';
import { BLOCK, MoveType } from './format';

const LOD_PIXEL = 0.2;
const TRAVEL_RADIUS = 0.1;
const WIPE_RADIUS = 0.1;

const SEGMENT_VS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp usampler2D;
uniform vec3 light_top_dir;
const float light_top_diffuse = 0.6 * 0.8;
const float light_top_specular = 0.6 * 0.25;
const float light_top_shininess = 20.0;
const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);
const float light_front_diffuse = 0.6 * 0.3;
const float ambient = 0.3;
const float emission = 0.15;
const vec3 UP = vec3(0, 0, 1);
uniform mat4 view_matrix;
uniform mat4 projection_matrix;
uniform vec3 camera_position;
uniform sampler2D position_tex;   // x, y, z, 0
uniform sampler2D attr_tex;       // height, width, type + 16 * role, 0
uniform sampler2D color_tex;      // rgba8
uniform usampler2D lod_tex;       // merged segment: start vertex, end vertex
uniform int first_instance;       // segment (or merged segment) of instance 0
uniform bool lod;
uniform bool ribbon;              // six corners of an eye-facing ribbon instead of the box's eight
uniform ivec2 range;              // visible segments [x, y)
uniform int role_mask;            // visible extrusion roles
uniform int type_mask;            // visible move types (travel, wipe, extrude)
uniform vec2 radii;               // travel, wipe
uniform int focus_mask;           // legend hover: roles drawn in full colour, the rest dimmed; -1 none
in float corner;
out vec3 color;
ivec2 tc(int id) { return ivec2(id % ${TEX_WIDTH}, id / ${TEX_WIDTH}); }
// A hovered legend row isolates its role: everything else fades to a flat grey (SB-16).
vec3 focused(vec3 c, int type, int role) {
  if (focus_mask == -1 || (type == ${MoveType.Extrude} && ((focus_mask >> role) & 1) != 0)) return c;
  return mix(vec3(0.5), vec3(dot(c, vec3(0.299, 0.587, 0.114))), 0.35);
}
float direct_lighting(vec3 eye_position, vec3 eye_normal) {
  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);
  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);
  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);
  return top_diffuse + front_diffuse + top_specular;
}
void main() {
  int vertex_id = int(corner);
  int id_a, id_b;
  if (lod) {
    uvec2 p = texelFetch(lod_tex, tc(first_instance + gl_InstanceID), 0).xy;
    id_a = int(p.x); id_b = int(p.y);
  } else {
    id_b = first_instance + gl_InstanceID;
    id_a = id_b - 1;
  }
  vec4 attr = texelFetch(attr_tex, tc(id_b), 0);
  int code = int(attr.z + 0.5);
  int type = code & 15;
  int role = code >> 4;
  // vertex 0 is the processor's dummy: nothing is drawn from it
  bool visible = id_a >= 1 && id_b >= range.x && id_b < range.y && id_a + 1 >= range.x &&
    ((type_mask >> type) & 1) != 0 && (type != ${MoveType.Extrude} || ((role_mask >> role) & 1) != 0);
  if (!visible) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); color = vec3(0.0); return; }
  vec2 hw = type == ${MoveType.Travel} ? vec2(radii.x) : type == ${MoveType.Wipe} ? vec2(radii.y) : attr.xy;
  vec3 pos_a = texelFetch(position_tex, tc(id_a), 0).xyz;
  vec3 pos_b = texelFetch(position_tex, tc(id_b), 0).xyz;
  // extrusion vertices sit half a bead below the nozzle; wipes are biased up against z-fighting
  vec3 shift = type == ${MoveType.Extrude} ? vec3(0.0, 0.0, -0.5 * hw.x) : type == ${MoveType.Wipe} ? vec3(0.0, 0.0, 0.05) : vec3(0.0);
  pos_a += shift; pos_b += shift;
  vec3 line = pos_b - pos_a;
  float line_len = length(line);
  vec3 line_dir = line_len < 1e-4 ? vec3(1.0, 0.0, 0.0) : line / line_len;
  vec3 line_right_dir = abs(dot(line_dir, UP)) > 0.9 ? normalize(cross(vec3(1, 0, 0), line_dir)) : normalize(cross(line_dir, UP));
  vec3 line_up_dir = normalize(cross(line_right_dir, line_dir));
  if (ribbon) {
    // A bead a pixel or two wide: a ribbon facing the eye, as wide as the bead's section seen from there, lit as
    // the box's section: along the sides at its edges, toward the eye along its axis (libvgcode Shaders.hpp).
    vec3 end_pos = vertex_id < 3 ? pos_a : pos_b;
    vec3 to_eye = camera_position - end_pos;
    vec3 facing = to_eye - dot(to_eye, line_dir) * line_dir;
    facing = dot(facing, facing) < 1e-12 ? line_up_dir : normalize(facing);
    vec3 side = cross(line_dir, facing);
    float half_extent = 0.5 * (hw.y * abs(dot(line_right_dir, side)) + hw.x * abs(dot(line_up_dir, side)));
    float s = float(vertex_id - 3 * (vertex_id / 3)) - 1.0;
    vec3 p = end_pos + s * half_extent * side;
    vec3 eye_p = (view_matrix * vec4(p, 1.0)).xyz;
    vec3 eye_n = mat3(view_matrix) * (s == 0.0 ? facing : s * side);
    vec3 base = focused(texelFetch(color_tex, tc(id_b), 0).rgb, type, role);
    color = base * (ambient + emission + direct_lighting(eye_p, eye_n));
    gl_Position = projection_matrix * vec4(eye_p, 1.0);
    return;
  }
  const vec2 signs_array[16] = vec2[](
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 0.0), vec2(0.0, -1.0),
    vec2(0.0, -1.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 0.0),
    vec2(0.0, 1.0), vec2(-1.0, 0.0), vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(-1.0, 0.0), vec2(0.0, 0.0));
  vec3 endpoint_pos = vertex_id < 4 ? pos_a : pos_b;
  vec3 closer_pos = dot(camera_position - pos_a, camera_position - pos_a) < dot(camera_position - pos_b, camera_position - pos_b) ? pos_a : pos_b;
  vec3 camera_view_dir = normalize(closer_pos - camera_position);
  vec3 diagonal_dir_border = normalize(hw.x * line_up_dir + hw.y * line_right_dir);
  bool is_vertical_view = abs(dot(camera_view_dir, line_up_dir)) / abs(dot(diagonal_dir_border, line_up_dir)) >
    abs(dot(camera_view_dir, line_right_dir)) / abs(dot(diagonal_dir_border, line_right_dir));
  vec2 signs = signs_array[vertex_id + 8 * int(is_vertical_view)];
  float view_right_sign = sign(dot(-camera_view_dir, line_right_dir));
  float view_top_sign = sign(dot(-camera_view_dir, line_up_dir));
  vec3 horizontal_dir = 0.5 * hw.y * line_right_dir;
  vec3 vertical_dir = 0.5 * hw.x * line_up_dir;
  vec3 pos = endpoint_pos + signs.x * view_right_sign * horizontal_dir + signs.y * view_top_sign * vertical_dir;
  if (vertex_id == 2 || vertex_id == 7)
    pos += (vertex_id == 2 ? -1.0 : 1.0) * line_dir * 0.5 * hw.y; // pointy caps
  vec3 eye_position = (view_matrix * vec4(pos, 1.0)).xyz;
  vec3 eye_normal = (view_matrix * vec4(normalize(pos - endpoint_pos), 0.0)).xyz;
  vec3 color_base = texelFetch(color_tex, tc(id_b), 0).rgb;
  color = color_base * (ambient + emission + direct_lighting(eye_position, eye_normal));
  gl_Position = projection_matrix * vec4(eye_position, 1.0);
}`;

const FS = `#version 300 es
precision highp float;
in vec3 color;
out vec4 frag;
void main() { frag = vec4(color, 1.0); }`;

const OPTION_VS = `#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp usampler2D;
uniform vec3 light_top_dir;
uniform mat4 view_matrix;
uniform mat4 projection_matrix;
uniform sampler2D position_tex;
uniform sampler2D attr_tex;
uniform sampler2D color_tex;
uniform usampler2D option_tex;
uniform ivec2 range;
uniform int type_mask;
in vec3 in_position;
in vec3 in_normal;
out vec3 color;
ivec2 tc(int id) { return ivec2(id % ${TEX_WIDTH}, id / ${TEX_WIDTH}); }
void main() {
  int id = int(texelFetch(option_tex, tc(gl_InstanceID), 0).x);
  vec4 attr = texelFetch(attr_tex, tc(id), 0);
  int type = int(attr.z + 0.5) & 15;
  if (id < range.x || id >= range.y || ((type_mask >> type) & 1) == 0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
  vec2 hw = max(attr.xy, vec2(0.2, 0.4)) * 1.5;
  vec3 p = texelFetch(position_tex, tc(id), 0).xyz - vec3(0.0, 0.0, 0.5 * hw.x / 1.5);
  vec3 pos = p + in_position * vec3(hw.y, hw.y, hw.x);
  vec3 eye_position = (view_matrix * vec4(pos, 1.0)).xyz;
  vec3 eye_normal = normalize(mat3(view_matrix) * in_normal);
  float l = 0.6 * 0.8 * max(dot(eye_normal, light_top_dir), 0.0) + 0.6 * 0.3 * max(dot(eye_normal, vec3(0.6985074, 0.1397015, 0.6985074)), 0.0);
  color = texelFetch(color_tex, tc(id), 0).rgb * (0.3 + 0.25 + l);
  gl_Position = projection_matrix * vec4(eye_position, 1.0);
}`;

function compile(gl: WebGL2RenderingContext, vs: string, fs: string): WebGLProgram {
  const sh = (type: number, src: string) => {
    const s = gl.createShader(type)!;
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s) ?? 'shader');
    return s;
  };
  const p = gl.createProgram()!;
  gl.attachShader(p, sh(gl.VERTEX_SHADER, vs));
  gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p) ?? 'link');
  return p;
}

const f32 = new Float32Array(1);
const u32 = new Uint32Array(f32.buffer);
function half(v: number): number {
  f32[0] = v;
  const x = u32[0];
  const sign = (x >>> 16) & 0x8000;
  let exp = ((x >>> 23) & 0xff) - 127 + 15;
  let mant = x & 0x7fffff;
  if (exp <= 0) return sign;
  if (exp >= 31) return sign | 0x7c00;
  mant += 0x1000;
  if (mant & 0x800000) { mant = 0; ++exp; }
  return sign | (exp << 10) | (mant >> 13);
}

export interface DrawState {
  range: [number, number];
  roleMask: number;
  typeMask: number;
  optionMask: number;
  focusMask?: number;
}

export interface FrameStats {
  blocks: number;
  culled: number;
  lodBlocks: number;
  segments: number;
  cpuMs: number;
}

interface Block {
  first: number;
  end: number;
  box: THREE.Box3;
}

export class SegmentRenderer {
  private gl: WebGL2RenderingContext;
  private prog: WebGLProgram;
  private optProg: WebGLProgram;
  private vao: WebGLVertexArrayObject;
  private optVao: WebGLVertexArrayObject;
  private ribbonVao: WebGLVertexArrayObject;
  private optIndexCount = 0;
  private u: Record<string, WebGLUniformLocation | null> = {};
  private ou: Record<string, WebGLUniformLocation | null> = {};
  private posTex: WebGLTexture;
  private attrTex: WebGLTexture;
  private colorTex: WebGLTexture;
  private lodTex: WebGLTexture;
  private optTex: WebGLTexture;
  private rows = 0;
  private lodUploaded = 0;
  private lodArrays = 0;
  private optCount = 0;
  private blocks: Block[] = [];
  private data: PreviewData | null = null;
  private attr = new Uint16Array(0);
  colors = new Uint8Array(0);
  stats: FrameStats = { blocks: 0, culled: 0, lodBlocks: 0, segments: 0, cpuMs: 0 };
  lodEnabled = true;
  lodPixel = LOD_PIXEL;

  constructor(private renderer: THREE.WebGLRenderer) {
    const gl = renderer.getContext() as WebGL2RenderingContext;
    this.gl = gl;
    this.prog = compile(gl, SEGMENT_VS, FS);
    this.optProg = compile(gl, OPTION_VS, FS);
    for (const n of ['light_top_dir', 'view_matrix', 'projection_matrix', 'camera_position', 'position_tex', 'attr_tex', 'color_tex',
      'lod_tex', 'first_instance', 'lod', 'ribbon', 'range', 'role_mask', 'type_mask', 'radii', 'focus_mask'])
      this.u[n] = gl.getUniformLocation(this.prog, n);
    for (const n of ['light_top_dir', 'view_matrix', 'projection_matrix', 'position_tex', 'attr_tex', 'color_tex', 'option_tex', 'range', 'type_mask'])
      this.ou[n] = gl.getUniformLocation(this.optProg, n);

    this.vao = gl.createVertexArray()!;
    gl.bindVertexArray(this.vao);
    const vb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 1, 2, 3, 4, 5, 6, 7]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(this.prog, 'corner');
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 1, gl.FLOAT, false, 0, 0);
    const ib = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint8Array([0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 6, 0, 6, 1, 5, 4, 7, 5, 7, 6]), gl.STATIC_DRAW);

    this.ribbonVao = gl.createVertexArray()!;
    gl.bindVertexArray(this.ribbonVao);
    const rb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, rb);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 1, 2, 3, 4, 5]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 1, gl.FLOAT, false, 0, 0);
    const rib = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, rib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint8Array([0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4]), gl.STATIC_DRAW);

    this.optVao = gl.createVertexArray()!;
    gl.bindVertexArray(this.optVao);
    const oct = new THREE.OctahedronGeometry(0.5, 0);
    oct.computeVertexNormals();
    const ob = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, ob);
    gl.bufferData(gl.ARRAY_BUFFER, oct.getAttribute('position').array as Float32Array, gl.STATIC_DRAW);
    const pl = gl.getAttribLocation(this.optProg, 'in_position');
    gl.enableVertexAttribArray(pl);
    gl.vertexAttribPointer(pl, 3, gl.FLOAT, false, 0, 0);
    const nb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, nb);
    gl.bufferData(gl.ARRAY_BUFFER, oct.getAttribute('normal').array as Float32Array, gl.STATIC_DRAW);
    const nl = gl.getAttribLocation(this.optProg, 'in_normal');
    gl.enableVertexAttribArray(nl);
    gl.vertexAttribPointer(nl, 3, gl.FLOAT, false, 0, 0);
    this.optIndexCount = oct.getAttribute('position').count;
    gl.bindVertexArray(null);

    this.posTex = gl.createTexture()!;
    this.attrTex = gl.createTexture()!;
    this.colorTex = gl.createTexture()!;
    this.lodTex = gl.createTexture()!;
    this.optTex = gl.createTexture()!;
    renderer.resetState();
  }

  private alloc(tex: WebGLTexture, internal: number, rows: number) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texStorage2D(gl.TEXTURE_2D, 1, internal, TEX_WIDTH, rows);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  }

  private upload(tex: WebGLTexture, format: number, type: number, comps: number, src: ArrayBufferView & ArrayLike<number>, from: number, to: number) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    const sub = (a: number, b: number) => (src as unknown as Float32Array).subarray(a * comps, b * comps);
    let i = from;
    while (i < to) {
      const row = Math.floor(i / TEX_WIDTH), col = i % TEX_WIDTH;
      if (col !== 0 || to - i < TEX_WIDTH) {
        const n = Math.min(TEX_WIDTH - col, to - i);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, col, row, n, 1, format, type, sub(i, i + n));
        i += n;
      } else {
        const rows = Math.floor((to - i) / TEX_WIDTH);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, row, TEX_WIDTH, rows, format, type, sub(i, i + rows * TEX_WIDTH));
        i += rows * TEX_WIDTH;
      }
    }
  }

  reset(d: PreviewData) {
    const gl = this.gl;
    this.data = d;
    for (const t of [this.posTex, this.attrTex, this.colorTex, this.lodTex, this.optTex]) gl.deleteTexture(t);
    this.posTex = gl.createTexture()!;
    this.attrTex = gl.createTexture()!;
    this.colorTex = gl.createTexture()!;
    this.lodTex = gl.createTexture()!;
    this.optTex = gl.createTexture()!;
    this.rows = d.rows;
    this.alloc(this.posTex, gl.RGBA32F, this.rows);
    this.alloc(this.attrTex, gl.RGBA16F, this.rows);
    this.alloc(this.colorTex, gl.RGBA8, this.rows);
    this.alloc(this.lodTex, gl.RG32UI, this.rows);
    this.alloc(this.optTex, gl.R32UI, this.rows);
    this.attr = new Uint16Array(d.n * 4);
    this.colors = new Uint8Array(d.n * 4);
    this.blocks = [];
    this.lodUploaded = 0;
    this.lodArrays = 0;
    this.optCount = 0;
    this.renderer.resetState();
  }

  addChunk(start: number, count: number) {
    const d = this.data!;
    const gl = this.gl;
    const end = start + count;
    const a = this.attr;
    for (let i = start; i < end; ++i) {
      a[i * 4] = half(d.height[i]);
      a[i * 4 + 1] = half(d.width[i]);
      a[i * 4 + 2] = half((d.type[i] & 0x0f) + 16 * d.role[i]);
    }
    this.upload(this.posTex, gl.RGBA, gl.FLOAT, 4, d.pos, start, end);
    this.upload(this.attrTex, gl.RGBA, gl.HALF_FLOAT, 4, a, start, end);
    for (let b = start; b < end; b += BLOCK) {
      const e = Math.min(end, b + BLOCK);
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      let pad = 0;
      for (let s = Math.max(1, b - 1); s < e; ++s) {
        if ((d.type[s] & 0x7f) === MoveType.Noop) continue;
        for (let c = 0; c < 3; ++c) {
          const v = d.pos[s * 4 + c];
          if (v < lo[c]) lo[c] = v;
          if (v > hi[c]) hi[c] = v;
        }
        if (d.width[s] > pad) pad = d.width[s];
      }
      const box = new THREE.Box3(new THREE.Vector3(...lo), new THREE.Vector3(...hi));
      box.expandByScalar(3 * Math.min(pad, 2));
      this.blocks.push({ first: Math.max(1, b), end: e, box });
    }
    for (; this.lodArrays < d.lod.length; ++this.lodArrays) {
      const pairs = d.lod[this.lodArrays];
      this.uploadOffset(this.lodTex, gl.RG_INTEGER, gl.UNSIGNED_INT, 2, pairs, this.lodUploaded);
      this.lodUploaded += pairs.length / 2;
    }
    if (d.optionIds.length > this.optCount) {
      const ids = Uint32Array.from(d.optionIds.slice(this.optCount));
      this.uploadOffset(this.optTex, gl.RED_INTEGER, gl.UNSIGNED_INT, 1, ids, this.optCount);
      this.optCount = d.optionIds.length;
    }
    this.renderer.resetState();
  }

  private uploadOffset(tex: WebGLTexture, format: number, type: number, comps: number, src: Uint32Array, at: number) {
    const n = src.length / comps;
    if (!n) return;
    const shifted = { subarray: (a: number, b: number) => src.subarray(a - at * comps, b - at * comps) } as unknown as Float32Array;
    this.upload(tex, format, type, comps, shifted, at, at + n);
  }

  uploadColors(from: number, to: number) {
    this.upload(this.colorTex, this.gl.RGBA, this.gl.UNSIGNED_BYTE, 4, this.colors, from, to);
    this.renderer.resetState();
  }

  render(camera: THREE.PerspectiveCamera | THREE.OrthographicCamera, state: DrawState, viewportHeight: number) {
    const d = this.data;
    if (!d || !this.blocks.length) return;
    const t0 = performance.now();
    const gl = this.gl;
    gl.useProgram(this.prog);
    gl.bindVertexArray(this.vao);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    const u = this.u;
    gl.uniformMatrix4fv(u.view_matrix, false, camera.matrixWorldInverse.elements);
    gl.uniformMatrix4fv(u.projection_matrix, false, camera.projectionMatrix.elements);
    gl.uniform3f(u.camera_position, camera.position.x, camera.position.y, camera.position.z);
    gl.uniform3f(u.light_top_dir, -0.4574957, 0.4574957, 0.7624929);
    gl.uniform2i(u.range, state.range[0], state.range[1]);
    gl.uniform1i(u.role_mask, state.roleMask);
    gl.uniform1i(u.type_mask, state.typeMask);
    gl.uniform2f(u.radii, TRAVEL_RADIUS, WIPE_RADIUS);
    gl.uniform1i(u.focus_mask, state.focusMask ?? -1);
    const bind = (unit: number, tex: WebGLTexture, loc: WebGLUniformLocation | null) => {
      gl.activeTexture(gl.TEXTURE0 + unit);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniform1i(loc, unit);
    };
    bind(0, this.posTex, u.position_tex);
    bind(1, this.attrTex, u.attr_tex);
    bind(2, this.colorTex, u.color_tex);
    bind(3, this.lodTex, u.lod_tex);

    const frustum = new THREE.Frustum().setFromProjectionMatrix(
      new THREE.Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse));
    const k = camera instanceof THREE.PerspectiveCamera ? (2 * Math.tan((camera.fov * Math.PI) / 360)) / Math.max(1, viewportHeight) : 0;
    const [r0, r1] = state.range;
    const stats: FrameStats = { blocks: 0, culled: 0, lodBlocks: 0, segments: 0, cpuMs: 0 };
    let lodMode = false, ribbonMode = false;
    gl.uniform1i(u.lod, 0);
    gl.uniform1i(u.ribbon, 0);
    const order = camera.getWorldDirection(new THREE.Vector3()).z < -0.3 ? -1 : 1;
    const n = this.blocks.length;
    for (let j = 0; j < n; ++j) {
      const bi = order > 0 ? j : n - 1 - j;
      const b = this.blocks[bi];
      const s0 = Math.max(b.first, r0), s1 = Math.min(b.end, r1);
      if (s0 >= s1) continue;
      if (!frustum.intersectsBox(b.box)) { ++stats.culled; continue; }
      ++stats.blocks;
      const lodRange = d.lodBlocks.length > bi * 2 + 1 ? [d.lodBlocks[bi * 2], d.lodBlocks[bi * 2 + 1]] : null;
      const far = this.lodEnabled && (camera instanceof THREE.OrthographicCamera ? (camera.top - camera.bottom) / camera.zoom / Math.max(1, viewportHeight) : b.box.distanceToPoint(camera.position) * k) >= this.lodPixel;
      const merged = far && lodRange != null && s0 === b.first && s1 === b.end;
      if (far !== ribbonMode) {
        gl.bindVertexArray(far ? this.ribbonVao : this.vao);
        gl.uniform1i(u.ribbon, far ? 1 : 0);
        ribbonMode = far;
      }
      if (merged !== lodMode) { gl.uniform1i(u.lod, merged ? 1 : 0); lodMode = merged; }
      const first = merged ? lodRange![0] : s0;
      const cnt = merged ? lodRange![1] - lodRange![0] : s1 - s0;
      gl.uniform1i(u.first_instance, first);
      gl.drawElementsInstanced(gl.TRIANGLES, far ? 12 : 24, gl.UNSIGNED_BYTE, 0, cnt);
      stats.segments += cnt;
      if (far) ++stats.lodBlocks;
    }

    if (this.optCount) {
      gl.useProgram(this.optProg);
      gl.bindVertexArray(this.optVao);
      const o = this.ou;
      gl.uniformMatrix4fv(o.view_matrix, false, camera.matrixWorldInverse.elements);
      gl.uniformMatrix4fv(o.projection_matrix, false, camera.projectionMatrix.elements);
      gl.uniform3f(o.light_top_dir, -0.4574957, 0.4574957, 0.7624929);
      gl.uniform2i(o.range, state.range[0], state.range[1]);
      gl.uniform1i(o.type_mask, state.optionMask);
      bind(0, this.posTex, o.position_tex);
      bind(1, this.attrTex, o.attr_tex);
      bind(2, this.colorTex, o.color_tex);
      gl.activeTexture(gl.TEXTURE4);
      gl.bindTexture(gl.TEXTURE_2D, this.optTex);
      gl.uniform1i(o.option_tex, 4);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, this.optIndexCount, this.optCount);
    }
    gl.bindVertexArray(null);
    this.renderer.resetState();
    stats.cpuMs = performance.now() - t0;
    this.stats = stats;
  }

  gpuBytes(): number {
    const d = this.data;
    if (!d) return 0;
    return this.rows * TEX_WIDTH * (16 + 8 + 4 + 8 + 4);
  }

  dispose() {
    const gl = this.gl;
    for (const t of [this.posTex, this.attrTex, this.colorTex, this.lodTex, this.optTex]) gl.deleteTexture(t);
    gl.deleteProgram(this.prog);
    gl.deleteProgram(this.optProg);
  }
}
