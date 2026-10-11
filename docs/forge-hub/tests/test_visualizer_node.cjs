"use strict";
const fs=require("node:fs"),path=require("node:path"),
 assert=require("node:assert/strict"),vm=require("node:vm");
global.window={};
vm.runInThisContext(fs.readFileSync(
 path.join(__dirname,"..","forge-mod-visualizer.js"),"utf8"));
const visualizer=window.ForgeModVisualizer;
const model=visualizer.parseObj("v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n");
assert.equal(model.kind,"OBJ");
assert.equal(model.vertices.length,3);
assert.equal(model.faces.length,1);
const scene=Buffer.alloc(4096);
scene.writeUInt32LE(4092,0);
scene.writeUInt32LE(128,4);
const hdr=4+128;
scene.writeUInt32LE(1,hdr);
scene.writeUInt32LE(9,hdr+4);
scene.writeUInt32LE(256,hdr+12);
scene.writeUInt32LE(512,hdr+16);
for(let i=0;i<9;i++){
 scene.writeUInt16LE(i,4+256+2*i);
 const p=4+512+i*16;
 scene.writeInt16LE(i*10,p);
 scene.writeInt16LE(i*2,p+2);
 scene.writeInt16LE(i*3,p+4);
 scene.writeUInt8(20,p+8);
 scene.writeUInt8(90,p+9);
 scene.writeUInt8(170,p+10);
}
const sceneView=scene.buffer.slice(scene.byteOffset,scene.byteOffset+scene.byteLength);
const map=visualizer.parseLev(sceneView);
assert.equal(map.kind,"LEV");
assert.equal(map.vertices.length,9);
assert.equal(map.faces.length,8);
assert.deepEqual(map.colors[0],[170,90,20]);
assert.throws(()=>visualizer.parseLev(new ArrayBuffer(100)),/Invalid LEV/);
assert.throws(()=>visualizer.parseObj("v 0 0 0\n"),/no previewable faces/);
const html=fs.readFileSync(path.join(__dirname,"..","studio.html"),"utf8");
assert(html.includes('id="modPreviewCanvas"'));
assert(html.includes("forge-mod-visualizer.js"));
const controller=fs.readFileSync(path.join(__dirname,"..","creator-studio.js"),"utf8");
assert(controller.includes("modVisualizer.selectSources(packagerSources"));
console.log("WEB_ALL_CATEGORIES_MODEL_PREVIEW_TESTS_OK");
