#pragma once

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="vi">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>VIPER Control</title>
<style>
  :root{--bg:#0f172a;--card:#111827;--muted:#94a3b8;--txt:#e5e7eb;--acc:#22c55e;--rot:#3b82f6;--stop:#ef4444;--amber:#f59e0b;}
  *{box-sizing:border-box} html,body{min-height:100%}
  body{margin:0;font-family:ui-sans-serif,system-ui,Arial;background:radial-gradient(1200px 800px at 50% -10%, #1f2937 0%, var(--bg) 60%);
       color:var(--txt);display:flex;flex-direction:column;align-items:center;justify-content:flex-start;padding:8px;gap:12px;overflow-y:auto}
  .card{width:min(520px,100%);background:linear-gradient(180deg,#0b1220 0%, var(--card) 100%);
        border:1px solid #1f2937;border-radius:16px;padding:18px 16px;box-shadow:0 10px 30px rgba(0,0,0,.35)}
  
  /* VIDEO FULL WIDTH RESPONSIVE */
  .cam-card{padding:8px;width:100%;max-width:100%;margin:0}
  .cam-container{position:relative;width:100%;padding-top:56.25%;background:#000;border-radius:8px;overflow:hidden}
  .cam-container iframe{position:absolute;top:0;left:0;width:100%;height:100%;border:0}
  
  .cam-status{text-align:center;margin-top:8px;font-size:12px;color:var(--muted)}
  .cam-status.offline{color:var(--stop)}
  .cam-status.online{color:var(--acc)}
  .obstacle-status{margin-top:10px;padding:10px;background:#0b1220;border:1px solid #1f2937;border-radius:8px;font-size:13px;line-height:1.5;min-height:60px}
  .obstacle-status .label{color:var(--muted);font-size:11px;margin-bottom:4px}
  .obstacle-status .value{font-weight:600}
  .obstacle-status .red{color:#ef4444}
  .obstacle-status .green{color:#22c55e}
  .obstacle-status .yellow{color:#f59e0b}
  h1{margin:0 0 2px;font-size:22px}
  h2{margin:0 0 8px;font-size:16px;font-weight:600}
  .subtitle{margin:0 0 6px;font-size:11px;color:var(--muted);font-style:italic}
  .muted{color:var(--muted);font-size:12px;margin-bottom:12px}
  .row{display:flex;gap:10px;align-items:center;margin-bottom:10px}
  select{background:#0b1220;color:var(--txt);border:1px solid #1f2937;border-radius:10px;padding:8px 10px;font-size:14px}
  .badge{padding:6px 10px;border-radius:999px;border:1px solid #1f2937;background:#0b1220;font-size:12px}
  .controls{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:8px}
  .btn{
    appearance:none;border:0;border-radius:12px;padding:16px 8px;font-size:18px;font-weight:600;color:#0b1220;cursor:pointer;
    background:linear-gradient(180deg,#e5e7eb,#cbd5e1);box-shadow:0 4px 0 rgba(0,0,0,.25);transition:transform .05s,filter .15s,box-shadow .15s;width:100%;
    user-select:none;-webkit-user-select:none;touch-action:none;min-height:56px;
  }
  .btn.acc{background:linear-gradient(180deg,#34d399,#22c55e)}
  .btn.rot{background:linear-gradient(180deg,#93c5fd,#3b82f6)}
  .btn.stop{background:linear-gradient(180deg,#fb7185,#ef4444);color:#fff}
  .btn.active{transform:translateY(2px);box-shadow:0 2px 0 rgba(0,0,0,.25);filter:brightness(1.03)}
  .speed{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:16px;align-items:center}
  .pill{grid-column:1/-1;text-align:center;background:#0b1220;border:1px solid #1f2937;border-radius:999px;padding:8px 10px;font-size:14px}
  .overlay{position:fixed;inset:0;background:rgba(0,0,0,.35);display:none;align-items:center;justify-content:center;pointer-events:none}
  .overlay.show{display:flex}
  .overlay .box{background:#0b1220;border:1px solid #1f2937;border-radius:12px;padding:12px 14px;color:var(--txt);font-size:14px}
  
  /* MOBILE OPTIMIZATION */
  @media (max-width: 768px) {
    body{padding:4px;gap:8px}
    .card{padding:12px;border-radius:12px}
    .cam-card{padding:6px;border-radius:12px}
    .cam-container{border-radius:6px}
    h1{font-size:20px}
    h2{font-size:15px}
    .btn{font-size:20px;padding:18px 8px;min-height:60px;border-radius:10px}
    .controls{gap:8px}
    .speed{gap:6px;margin-top:12px}
    .muted{font-size:11px;margin-bottom:8px}
  }
  
  /* LANDSCAPE MOBILE - Video còn lớn hơn */
  @media (max-width: 768px) and (orientation: landscape) {
    .cam-card{width:100%;max-width:100%}
    .cam-container{padding-top:45%}
  }
  
  /* VERY SMALL SCREEN */
  @media (max-width: 380px) {
    .btn{font-size:18px;padding:14px 6px;min-height:52px}
    .pill{font-size:12px;padding:6px 8px}
  }
</style>
</head>
<body>
  <div class="card cam-card">
    <h2>VIPER CAM</h2>
    <div class="cam-container">
      <iframe id="camFrame" src="http://192.168.4.3:81/stream" allow="camera"></iframe>
    </div>
    <div id="camStatus" class="cam-status">Loading camera...</div>
    <div id="obstacleStatus" class="obstacle-status">
      <div class="label">OBSTACLE DETECTION STATUS</div>
      <div class="value">Chờ phát hiện vật cản...</div>
    </div>
  </div>

  <div class="card">
    <h2>🤖 OpenCV Shape Detection</h2>
    <div style="display:flex;gap:12px;align-items:center;padding:12px;background:#0b1220;border:1px solid #1f2937;border-radius:12px;">
      <div style="flex:1">
        <div style="font-size:13px;color:var(--muted);margin-bottom:4px;">Detected Shape:</div>
        <div id="colorValue" style="font-size:20px;font-weight:600;">none</div>
      </div>
      <div style="flex:1">
        <div style="font-size:13px;color:var(--muted);margin-bottom:4px;">Last Detection:</div>
        <div id="colorAge" style="font-size:14px;">--</div>
      </div>
      <div id="colorIndicator" style="width:56px;height:56px;display:flex;align-items:center;justify-content:center;font-size:32px;background:#1f2937;border:2px solid #374151;border-radius:12px;">🔲</div>
    </div>
    <div style="margin-top:10px;font-size:12px;color:var(--muted);text-align:center;padding:8px;background:#0b1220;border:1px solid #1f2937;border-radius:8px;">
      <div style="margin-bottom:4px;font-weight:600;color:#3b82f6;">📊 OpenCV + HoughCircles + Contour</div>
      <span style="color:#22c55e;">🔵</span> Circle → Rẽ PHẢI &nbsp;|&nbsp; 
      <span style="color:#ef4444;">⬜</span> Square → Rẽ TRÁI
    </div>
  </div>

  <div class="card">
    <h1>VIPER</h1>
    <div class="subtitle">Vision Intelligent Path Exploration Robot</div>
    <div class="muted">Hold to drive. Release to stop. Multi-touch enabled.</div>

    <div class="row">
      <label for="modeSel">Mode:</label>
      <select id="modeSel">
        <option value="manual">Manual</option>
        <option value="line">Line follow</option>
      </select>
      <span id="modeBadge" class="badge">mode: manual</span>
    </div>

    <div class="controls">
      <button class="btn acc hold" data-path="/fwd_left">&#8598;</button>
      <button class="btn acc hold" data-path="/forward">&#8593;</button>
      <button class="btn acc hold" data-path="/fwd_right">&#8599;</button>
      <button class="btn rot hold" data-path="/left">&#8592;</button>
      <button class="btn stop" id="stopBtn" data-path="/stop">&#9632;</button>
      <button class="btn rot hold" data-path="/right">&#8594;</button>
      <button class="btn acc hold" data-path="/back_left">&#8601;</button>
      <button class="btn acc hold" data-path="/backward">&#8595;</button>
      <button class="btn acc hold" data-path="/back_right">&#8600;</button>
    </div>

    <div class="speed">
      <div class="pill" id="spdText">Lin: --  |  Rot: --</div>
      <button class="btn spd" data-path="/speed/lin/down">Lin -</button>
      <button class="btn spd" data-path="/speed/lin/up">Lin +</button>
      <button class="btn spd" data-path="/speed/rot/down">Rot -</button>
      <button class="btn spd" data-path="/speed/rot/up">Rot +</button>
    </div>
  </div>

  <div id="overlay" class="overlay"><div class="box">Line follow mode active. Manual control disabled.</div></div>

<script>
  document.addEventListener('contextmenu', e=>e.preventDefault());
  const overlay = document.getElementById('overlay');
  const modeSel = document.getElementById('modeSel');
  const modeBadge = document.getElementById('modeBadge');

  function uiLock(isLocked){
    overlay.classList.toggle('show', isLocked);
    document.querySelectorAll('.hold, .spd, #stopBtn')
      .forEach(b => b.disabled = isLocked);
  }
  async function send(path){ try{ await fetch(path); }catch(e){} }
  async function refreshSpeed(){
    try{ const r=await fetch('/speed'); document.getElementById('spdText').textContent=await r.text(); }catch(e){}
  }
  async function refreshMode(){
    try{
      const r = await fetch('/getMode'); const m = await r.text();
      modeSel.value = m; modeBadge.textContent = 'mode: ' + m;
      uiLock(m !== 'manual');
    }catch(e){}
  }
  modeSel.addEventListener('change', async ()=>{
    try{
      const r = await fetch('/setMode?m=' + modeSel.value);
      const m = await r.text();
      modeBadge.textContent = 'mode: ' + m;
      uiLock(m !== 'manual');
    }catch(e){}
  });

  let activeHold = { btn:null, pointerId:null };
  function guardManual(handler){
    return function(e){
      if (modeSel.value !== 'manual') { e.preventDefault(); return; }
      return handler(e);
    }
  }
  document.querySelectorAll('.hold').forEach(btn=>{
    btn.addEventListener('pointerdown', guardManual(e=>{
      e.preventDefault();
      activeHold = { btn, pointerId: e.pointerId };
      btn.classList.add('active');
      btn.setPointerCapture(e.pointerId);
      send(btn.dataset.path);
    }), {passive:false});
    const release = guardManual(e=>{
      e.preventDefault();
      if (activeHold.btn === btn && activeHold.pointerId === e.pointerId) {
        btn.classList.remove('active');
        send('/stop');
        activeHold = { btn:null, pointerId:null };
      }
      try{ btn.releasePointerCapture(e.pointerId); }catch(_){}
    });
    btn.addEventListener('pointerup', release, {passive:false});
    btn.addEventListener('pointercancel', release, {passive:false});
    btn.addEventListener('pointerleave', release, {passive:false});
  });

  document.getElementById('stopBtn').addEventListener('pointerdown', guardManual(e=>{
    e.preventDefault();
    send('/stop');
  }), {passive:false});

  document.querySelectorAll('.spd').forEach(b=>{
    b.addEventListener('pointerdown', guardManual(async e=>{
      e.preventDefault(); await send(b.dataset.path); refreshSpeed();
    }), {passive:false});
  });

  const camFrame = document.getElementById('camFrame');
  const camStatus = document.getElementById('camStatus');
  let camCheckInterval;

  function checkCamera() {
    fetch('http://192.168.4.3:81/stream', {method:'HEAD', mode:'no-cors'})
      .then(()=> {
        camStatus.textContent = 'Camera online';
        camStatus.className = 'cam-status online';
      })
      .catch(()=> {
        camStatus.textContent = 'Camera offline';
        camStatus.className = 'cam-status offline';
      });
  }

  camFrame.addEventListener('load', ()=> {
    camStatus.textContent = 'Livestream running';
    camStatus.className = 'cam-status online';
  });

  camFrame.addEventListener('error', ()=> {
    camStatus.textContent = 'Failed to load stream';
    camStatus.className = 'cam-status offline';
  });

  checkCamera();
  camCheckInterval = setInterval(checkCamera, 5000);

  // Obstacle status update
  async function refreshObstacleStatus(){
    try{
      const r = await fetch('/api/obstacle_status');
      const data = await r.json();
      const obstacleStatusDiv = document.getElementById('obstacleStatus');
      
      if (data.time === 0 || data.action === 'none') {
        obstacleStatusDiv.innerHTML = '<div class="label">OBSTACLE DETECTION STATUS</div><div class="value">Chờ phát hiện vật cản...</div>';
        return;
      }
      
      const age = Date.now() - data.time;
      let statusText = `<div class="label">OBSTACLE DETECTION (${(age/1000).toFixed(1)}s ago)</div>`;
      statusText += `<div style="margin-top:6px">⚠️ <strong>Vật cản: ${data.distance.toFixed(1)} cm</strong></div>`;
      
      if (data.action === 'square_right') {
        statusText += '<div style="margin-top:4px"><span style="color:#ef4444">⬛ HÌNH VUÔNG</span> → Rẽ <strong>PHẢI</strong> ➡️</div>';
      } else if (data.action === 'circle_left') {
        statusText += '<div style="margin-top:4px"><span style="color:#22c55e">⚫ HÌNH TRÒN</span> → Rẽ <strong>TRÁI</strong> ⬅️</div>';
      } else if (data.action === 'unknown_left') {
        statusText += '<div style="margin-top:4px"><span class="yellow">❓ Không nhận diện</span> → Rẽ <strong>TRÁI</strong> (mặc định)</div>';
      } else if (data.action === 'white_right') {
        statusText += '<div style="margin-top:4px"><span style="color:#e5e7eb">● TRẮNG (backup)</span> → Rẽ <strong>PHẢI</strong></div>';
      } else if (data.action === 'dark_left') {
        statusText += '<div style="margin-top:4px"><span style="color:#9ca3af">● ĐEN (backup)</span> → Rẽ <strong>TRÁI</strong></div>';
      }
      
      obstacleStatusDiv.innerHTML = statusText;
    }catch(e){console.error('Obstacle status error:', e);}
  }

  refreshMode();
  refreshSpeed();
  setInterval(refreshObstacleStatus, 500); // Cập nhật mỗi 500ms

  // OpenCV Shape detection update
  async function refreshColor(){
    try{
      const r = await fetch('/api/color');
      const data = await r.json();
      const colorValue = document.getElementById('colorValue');
      const colorAge = document.getElementById('colorAge');
      const colorIndicator = document.getElementById('colorIndicator');
      
      const shape = data.color;
      
      // Display shape name
      if (shape === 'circle') {
        colorValue.textContent = '🔵 Circle (Turn RIGHT)';
        colorValue.style.color = '#22c55e';
      } else if (shape === 'square') {
        colorValue.textContent = '⬜ Square (Turn LEFT)';
        colorValue.style.color = '#ef4444';
      } else {
        colorValue.textContent = shape;
        colorValue.style.color = 'var(--muted)';
      }
      
      // Update age display
      const ageMs = data.age;
      if (ageMs < 1000) {
        colorAge.textContent = ageMs + ' ms';
      } else if (ageMs < 60000) {
        colorAge.textContent = (ageMs/1000).toFixed(1) + ' s';
      } else {
        colorAge.textContent = (ageMs/60000).toFixed(1) + ' min';
      }
      
      // Update shape indicator
      if (shape === 'left') {
        colorIndicator.textContent = '⚫';
        colorIndicator.style.background = 'linear-gradient(135deg, #34d399, #22c55e)';
        colorIndicator.style.borderColor = '#22c55e';
        colorIndicator.style.fontSize = '36px';
      } else if (shape === 'right') {
        colorIndicator.textContent = '⬛';
        colorIndicator.style.background = 'linear-gradient(135deg, #fb7185, #ef4444)';
        colorIndicator.style.borderColor = '#ef4444';
        colorIndicator.style.fontSize = '36px';
      } else {
        colorIndicator.textContent = '🔲';
        colorIndicator.style.background = '#1f2937';
        colorIndicator.style.borderColor = '#374151';
        colorIndicator.style.fontSize = '32px';
      }
    }catch(e){}
  }
  
  refreshColor();
  setInterval(refreshColor, 1000);
</script>
</body>
</html>
)rawliteral";
