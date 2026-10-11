// web_interface.h
#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>RoboExhibition Dashboard</title>
  <style>
    body { font-family: Arial; text-align: center; background: #222; color: white; }
    img { max-width: 100%; border: 2px solid #555; }
    .btn { padding: 15px 20px; margin: 5px; font-size: 18px; border: none; border-radius: 5px; cursor: pointer; }
    .btn-blue { background: #007bff; color: white; }
    .btn-red { background: #dc3545; color: white; }
    .slider { width: 80%; margin: 20px auto; }
  </style>
</head>
<body>
  <h2>Robot Control Panel</h2>
  <img src="/stream" id="camStream" />
  
  <div>
    <button class="btn btn-blue" onmousedown="drive(1,1)" onmouseup="drive(0,0)" ontouchstart="drive(1,1)" ontouchend="drive(0,0)">Forward (W)</button><br>
    <button class="btn btn-blue" onmousedown="drive(-1,1)" onmouseup="drive(0,0)" ontouchstart="drive(-1,1)" ontouchend="drive(0,0)">Left (A)</button>
    <button class="btn btn-red" onmousedown="drive(0,0)" ontouchstart="drive(0,0)">Stop (S)</button>
    <button class="btn btn-blue" onmousedown="drive(1,-1)" onmouseup="drive(0,0)" ontouchstart="drive(1,-1)" ontouchend="drive(0,0)">Right (D)</button><br>
    <button class="btn btn-blue" onmousedown="drive(-1,-1)" onmouseup="drive(0,0)" ontouchstart="drive(-1,-1)" ontouchend="drive(0,0)">Backward (X)</button>
  </div>
  
  <div>
    <label>Head Pan:</label>
    <input type="range" min="0" max="180" value="90" class="slider" oninput="setServo('head', this.value)">
    <label>Arm Tilt:</label>
    <input type="range" min="0" max="180" value="90" class="slider" oninput="setServo('arm', this.value)">
  </div>

  <button class="btn btn-red" id="pttBtn" onmousedown="talk(true)" onmouseup="talk(false)" ontouchstart="talk(true)" ontouchend="talk(false)">Push to Talk</button>

  <script>
    // Keyboard WASD setup
    document.addEventListener('keydown', (e) => {
        if(e.key === 'w') drive(1,1);
        if(e.key === 's') drive(0,0);
        if(e.key === 'a') drive(-1,1);
        if(e.key === 'd') drive(1,-1);
        if(e.key === 'x') drive(-1,-1);
    });
    document.addEventListener('keyup', () => { drive(0,0); });

    function drive(l, r) { fetch(`/action?l=${l}&r=${r}`); }
    function setServo(id, val) { fetch(`/servo?id=${id}&val=${val}`); }
    function talk(state) { fetch(`/ptt?active=${state ? 1 : 0}`); }
  </script>
</body>
</html>
)rawliteral";

#endif