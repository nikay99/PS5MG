"""Renders a GLB with three.js (PBR light) in headless Chromium: textured or grey clay.
usage: python render-glb.py model.glb out.png [clay] [rotx_deg]    needs: pip install playwright && playwright install chromium
Internet is needed once (three.js from a CDN). Used for the README pictures."""
import http.server, json, os, sys, threading, functools
from playwright.sync_api import sync_playwright

glb, out = os.path.abspath(sys.argv[1]), sys.argv[2]
clay = "clay" in sys.argv[3:]
alpha = "alpha" in sys.argv[3:]   # transparent background (icons)
single = "single" in sys.argv[3:]  # one 1000x1000 view instead of three
rotx = next((float(a) for a in sys.argv[3:] if a.lstrip("-").replace(".", "").isdigit()), 0.0)
d = os.path.dirname(glb)
H = functools.partial(http.server.SimpleHTTPRequestHandler, directory=d)
H.log_message = lambda *a, **k: None
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
port = srv.server_address[1]
html = """<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:%BG%}canvas{display:block}</style>
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script>
<script type="module">
import * as THREE from 'three';
import {GLTFLoader} from 'three/addons/loaders/GLTFLoader.js';
import {RoomEnvironment} from 'three/addons/environments/RoomEnvironment.js';
const W=%W%,Hh=%H%,CLAY=%CLAY%,ROTX=%ROTX%;
const r=new THREE.WebGLRenderer({antialias:true,alpha:true,preserveDrawingBuffer:true});r.setSize(W,Hh);r.setPixelRatio(1);
r.toneMapping=THREE.ACESFilmicToneMapping;r.outputColorSpace=THREE.SRGBColorSpace;document.body.appendChild(r.domElement);
const pm=new THREE.PMREMGenerator(r);const scene=new THREE.Scene();if(!%ALPHA%)scene.background=new THREE.Color(0xeceef1);else r.setClearColor(0x000000,0);
scene.environment=pm.fromScene(new RoomEnvironment(),0.04).texture;
const key=new THREE.DirectionalLight(0xffffff,2.2);key.position.set(3,5,4);scene.add(key);
const g=await new GLTFLoader().loadAsync('/model.glb');const obj=g.scene;obj.rotation.x=THREE.MathUtils.degToRad(ROTX);
obj.traverse(o=>{if(o.isMesh){if(CLAY){o.material=new THREE.MeshStandardMaterial({color:0xc9ccd2,roughness:0.55,metalness:0});}else{o.material.side=THREE.DoubleSide;if(o.material.metalness!==undefined)o.material.metalness=Math.min(o.material.metalness,0.4);}}});
const pivot=new THREE.Group();pivot.add(obj);scene.add(pivot);
const box=new THREE.Box3().setFromObject(pivot);const c=box.getCenter(new THREE.Vector3());const s=box.getSize(new THREE.Vector3());
obj.position.sub(c);const size=Math.max(s.x,s.y,s.z);
const NV=%NV%;const cam=new THREE.PerspectiveCamera(30,(W/NV)/Hh,0.01,100);cam.position.set(0,size*0.3,size*%DIST%);cam.lookAt(0,0,0);
const views=NV==1?[[-0.6,0]]:[[-0.8,0],[0.3,0],[2.3,0]];
const sc=[];r.setScissorTest(true);
views.forEach((v,i)=>{pivot.rotation.y=v[0];const x=i*(W/NV);r.setViewport(x,0,W/NV,Hh);r.setScissor(x,0,W/NV,Hh);r.render(scene,cam);});
window.DONE=true;
</script>""".replace("%BG%", "transparent" if alpha else "#eceef1").replace("%W%", "1000" if single else "1800").replace("%H%", "1000" if single else "600").replace("%ALPHA%", "true" if alpha else "false").replace("%DIST%", os.environ.get("RENDER_DIST", "2.55")).replace("%NV%", "1" if single else "3").replace("%CLAY%", "true" if clay else "false").replace("%ROTX%", str(rotx))
open(os.path.join(d, "_render.html"), "w").write(html)
os.replace(glb, os.path.join(d, "model.glb")) if False else None
import shutil; shutil.copyfile(glb, os.path.join(d, "model.glb"))
with sync_playwright() as p:
    b = p.chromium.launch(args=["--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"])
    for attempt in range(5):  # a render now and then comes out blank (page shown before the module finished): try again
        pg = b.new_page(viewport={"width": 1000 if single else 1800, "height": 1000 if single else 600})
        pg.goto(f"http://127.0.0.1:{port}/_render.html")
        pg.wait_for_function("window.DONE===true", timeout=120000)
        pg.screenshot(path=out, omit_background=alpha)
        pg.close()
        if os.path.getsize(out) > (8000 if alpha else 20000):
            break
    b.close()
os.remove(os.path.join(d, "_render.html")); os.remove(os.path.join(d, "model.glb"))
print("saved", out)
