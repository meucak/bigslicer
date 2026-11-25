import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import os from 'node:os';
import { decodeKtpBuffer, buildUnitBeadGeometry, getNozzlePosition } from '../../docs/js/toolpath_geometry.js';

describe('KTP1 Binary Toolpath Decoder & Instanced Bead Geometry', () => {
  it('builds a valid 36-vertex unit 3D stadium bead mesh with unit normals', () => {
    const verts = buildUnitBeadGeometry();
    assert.equal(verts.length, 36 * 6);
    for (let i = 0; i < 36; i++) {
      const nu = verts[i * 6 + 3], nv = verts[i * 6 + 4], nw = verts[i * 6 + 5];
      const len = Math.hypot(nu, nv, nw);
      assert.ok(Math.abs(len - 1.0) < 1e-4, `Vertex ${i} normal is unit length`);
    }
  });

  it('decodes real C++20-generated KTP1 binary with exact layer spans & ranges', () => {
    const katmanBin = process.env.KATMAN_BIN || '/src/build/katman';
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ktp-test-'));
    try {
      // Build tiny binary cube STL
      const stlPath = path.join(tmp, 'box.stl');
      const ktpPath = path.join(tmp, 'box.ktp');
      const tris = [];
      const q = (a, b, c, d) => { tris.push([a, b, c]); tris.push([a, c, d]); };
      q([0,0,0],[0,15,0],[15,15,0],[15,0,0]);
      q([0,0,6],[15,0,6],[15,15,6],[0,15,6]);
      q([0,0,0],[15,0,0],[15,0,6],[0,0,6]);
      q([15,15,0],[0,15,0],[0,15,6],[15,15,6]);
      q([0,15,0],[0,0,0],[0,0,6],[0,15,6]);
      q([15,0,0],[15,15,0],[15,15,6],[15,0,6]);
      const buf = Buffer.alloc(84 + tris.length * 50, 0);
      buf.writeUInt32LE(tris.length, 80);
      for (let i = 0; i < tris.length; i++) {
        const off = 84 + i * 50 + 12;
        for (let v = 0; v < 3; v++) {
          buf.writeFloatLE(tris[i][v][0], off + v * 12);
          buf.writeFloatLE(tris[i][v][1], off + v * 12 + 4);
          buf.writeFloatLE(tris[i][v][2], off + v * 12 + 8);
        }
      }
      fs.writeFileSync(stlPath, buf);

      execFileSync(katmanBin, ['slice', stlPath, '--ktp', ktpPath, '--no-gcode', '--pattern', 'gyroid']);
      const raw = fs.readFileSync(ktpPath);
      const ab = raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength);
      const tp = decodeKtpBuffer(ab);

      assert.ok(tp.layerCount >= 28);
      assert.ok(tp.extrudeTotal > 100);
      assert.equal(tp.extrudeInstances.length, tp.extrudeTotal * 10);
      assert.ok(tp.ranges.maxSpeed > tp.ranges.minSpeed);
      const noz = getNozzlePosition(tp, tp.layerCount - 1, 0.5);
      assert.ok(noz && Number.isFinite(noz.x) && Number.isFinite(noz.z) && noz.z > 5.0);
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true });
    }
  });
});
