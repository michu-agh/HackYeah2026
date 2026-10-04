from flask import Flask, jsonify, request, Response
import serial
import threading
import time
import os
import hmac
from collections import deque
from datetime import datetime

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 4096

SERIAL_PORT = os.getenv("CRISIS_SERIAL_PORT", "/dev/ttyAMA0")
BAUD_RATE = int(os.getenv("CRISIS_BAUD_RATE", "115200"))
MAX_MESSAGE_BYTES = 222
ACCESS_TOKEN = os.getenv("CRISIS_MESH_TOKEN", "").strip()

ser = None
serial_lock = threading.Lock()
metrics_lock = threading.Lock()
started_at = time.time()
tx_packets = 0
last_tx_at = None
last_error = None
history = deque(maxlen=25)

PAGE = r"""
<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CrisisMesh</title>
<style>
:root{
    --bg:#080b11;
    --panel:#0f1624;
    --panel2:#0a101a;
    --border:#5b1e24;
    --border2:#263246;
    --red:#ef4444;
    --red2:#ff2a2a;
    --green:#10b981;
    --orange:#f59e0b;
    --blue:#60a5fa;
    --text:#f3f4f6;
    --muted:#9ca3af;
}
*{box-sizing:border-box}
body{
    margin:0;
    min-height:100vh;
    background:
        radial-gradient(circle at 15% 15%,rgba(239,68,68,.09),transparent 34%),
        radial-gradient(circle at 85% 10%,rgba(96,165,250,.04),transparent 28%),
        var(--bg);
    color:var(--text);
    font-family:Arial,sans-serif;
}
header{
    padding:18px 28px;
    border-bottom:1px solid var(--border);
    display:flex;
    justify-content:space-between;
    align-items:center;
    gap:16px;
    position:sticky;
    top:0;
    z-index:10;
    background:rgba(8,11,17,.94);
    backdrop-filter:blur(10px);
}
h1{margin:0;font-size:24px;letter-spacing:2px}
.sub{font-size:12px;color:var(--muted);margin-top:4px}
.gateway{font:12px monospace;color:var(--muted);text-align:right}
.wrap{
    max-width:1180px;
    margin:26px auto;
    padding:0 18px;
    display:grid;
    grid-template-columns:300px 1fr;
    gap:20px;
}
.panel{
    background:rgba(15,22,36,.88);
    border:1px solid var(--border2);
    border-radius:10px;
    padding:18px;
    margin-bottom:18px;
    box-shadow:0 10px 30px rgba(0,0,0,.16);
}
.panel.emergency{border-color:var(--border)}
.panel h2{
    margin:0 0 16px;
    font-size:14px;
    text-transform:uppercase;
    letter-spacing:1.2px;
}
.row{
    display:flex;
    justify-content:space-between;
    gap:16px;
    padding:10px 0;
    border-bottom:1px solid rgba(255,255,255,.05);
    font-size:13px;
}
.row:last-child{border-bottom:0}
.value{font-family:monospace;text-align:right}
.dot{
    width:9px;height:9px;border-radius:50%;
    display:inline-block;margin-right:8px;
    background:var(--red);
    color:var(--red);
    box-shadow:0 0 8px currentColor;
}
.dot.online{background:var(--green);color:var(--green)}
.dot.warn{background:var(--orange);color:var(--orange)}
label{
    display:block;
    color:var(--muted);
    font-size:12px;
    margin:12px 0 6px;
    text-transform:uppercase;
}
textarea,input{
    width:100%;
    background:#090d15;
    color:white;
    border:1px solid #2a3444;
    border-radius:7px;
    padding:13px;
    font-size:15px;
    outline:none;
}
textarea:focus,input:focus{border-color:#6b2c34;box-shadow:0 0 0 2px rgba(239,68,68,.08)}
textarea{min-height:170px;resize:vertical;line-height:1.45}
.bytebar{
    display:flex;
    justify-content:space-between;
    gap:12px;
    margin-top:8px;
    font:12px monospace;
    color:var(--muted);
}
.bytebar .bad{color:var(--red2)}
.bytebar .near{color:var(--orange)}
.actions{display:grid;grid-template-columns:1fr auto auto;gap:10px;margin-top:14px}
button{
    min-height:44px;
    border:1px solid transparent;
    border-radius:7px;
    color:white;
    background:#182131;
    font-weight:700;
    letter-spacing:.5px;
    cursor:pointer;
    padding:0 16px;
}
button:hover{filter:brightness(1.08)}
button:disabled{opacity:.45;cursor:not-allowed;filter:none}
button.primary{background:linear-gradient(135deg,var(--red),#b91c1c);letter-spacing:1px}
button.ghost{border-color:#2a3444;background:#0b111b}
.quickgrid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.quick{
    min-height:38px;
    font-size:12px;
    padding:0 10px;
    background:#0b111b;
    border-color:#293447;
}
.quick.sos{border-color:#7f1d1d;background:#221013;color:#fecaca}
#sendStatus{
    margin-top:12px;
    min-height:20px;
    font-family:monospace;
    font-size:13px;
}
#log{
    background:#04060a;
    border:1px solid #222b39;
    border-radius:7px;
    min-height:210px;
    max-height:330px;
    overflow:auto;
    padding:12px;
    font-family:monospace;
    font-size:12px;
}
.logrow{margin-bottom:9px;padding-bottom:9px;border-bottom:1px solid rgba(255,255,255,.04);line-height:1.35}
.logrow:last-child{border-bottom:0}
.time{color:#6b7280;margin-right:8px}
.tx{color:var(--red2);margin-right:8px}
.txid{color:var(--blue);margin-right:8px}
.empty{color:#6b7280}
.authbox{display:none}
.note{font-size:12px;line-height:1.45;color:var(--muted);margin-top:10px}
.kbd{font-family:monospace;border:1px solid #374151;background:#111827;padding:2px 5px;border-radius:4px;color:#d1d5db}
@media(max-width:850px){
    .wrap{grid-template-columns:1fr}
    .quickgrid{grid-template-columns:1fr 1fr}
}
@media(max-width:560px){
    header{padding:14px 16px}
    .gateway{display:none}
    .actions{grid-template-columns:1fr 1fr}
    .actions .primary{grid-column:1/-1}
}
</style>
</head>
<body>
<header>
    <div>
        <h1>CRISIS MESH</h1>
        <div class="sub">EMERGENCY MESSAGE GATEWAY</div>
    </div>
    <div class="gateway">LOCAL GATEWAY<br><span id="headerStatus">CHECKING...</span></div>
</header>

<div class="wrap">
    <aside>
        <div class="panel">
            <h2>Gateway status</h2>
            <div class="row">
                <span>UART interface</span>
                <span><span id="serialDot" class="dot"></span><b id="serialText">CHECKING</b></span>
            </div>
            <div class="row"><span>Device</span><span id="serialDevice" class="value">-</span></div>
            <div class="row"><span>Baud</span><span id="baudRate" class="value">-</span></div>
            <div class="row"><span>Uptime</span><span id="uptime" class="value">-</span></div>
            <div class="row"><span>Last TX</span><span id="lastTx" class="value">-</span></div>
        </div>

        <div class="panel">
            <h2>Metrics</h2>
            <div class="row"><span>TX packets</span><b id="sentCount" class="value">0</b></div>
            <div class="row"><span>Payload limit</span><b class="value">222 B UTF-8</b></div>
        </div>

        <div class="panel authbox" id="authBox">
            <h2>Operator access</h2>
            <label for="token">Access token</label>
            <input id="token" type="password" autocomplete="off" placeholder="CRISIS_MESH_TOKEN">
            <div class="note">Token jest trzymany tylko w tej karcie przegladarki.</div>
        </div>
    </aside>

    <main>
        <div class="panel emergency">
            <h2>Transmit message</h2>

            <div class="quickgrid">
                <button class="quick sos" data-template="SOS: potrzebna natychmiastowa pomoc. Lokalizacja: ">SOS</button>
                <button class="quick" data-template="EWAKUACJA: opusc wskazany obszar i kieruj sie do: ">EWAKUACJA</button>
                <button class="quick" data-template="ZAGROZENIE: unikaj obszaru: ">ZAGROZENIE</button>
                <button class="quick" data-template="STATUS: sytuacja pod kontrola. Dalsze informacje: ">STATUS</button>
            </div>

            <label for="message">Message text</label>
            <textarea id="message" placeholder="Wpisz komunikat..." autofocus></textarea>

            <div class="bytebar">
                <span><span id="byteCount">0</span> / 222 B</span>
                <span>Wyslij: <span class="kbd">Ctrl</span> + <span class="kbd">Enter</span></span>
            </div>

            <div class="actions">
                <button id="sendButton" class="primary">TRANSMIT MESSAGE</button>
                <button id="repeatButton" class="ghost" type="button">REPEAT LAST</button>
                <button id="clearButton" class="ghost" type="button">CLEAR</button>
            </div>

            <div id="sendStatus"></div>
            <div class="note">Do UART trafia wylacznie tekst komunikatu zakonczony znakiem nowej linii.</div>
        </div>

        <div class="panel">
            <h2>Recent transmissions</h2>
            <div id="log"><span class="empty">Brak transmisji w tej sesji serwera.</span></div>
        </div>
    </main>
</div>

<script>
const $ = id => document.getElementById(id);
const encoder = new TextEncoder();
let lastMessage = "";
let authRequired = false;

function escapeHtml(value){
    return String(value)
        .replaceAll("&","&amp;")
        .replaceAll("<","&lt;")
        .replaceAll(">","&gt;")
        .replaceAll('"',"&quot;")
        .replaceAll("'","&#039;");
}

function messageBytes(){
    return encoder.encode($("message").value).length;
}

function updateByteCount(){
    const bytes = messageBytes();
    const el = $("byteCount");
    el.textContent = bytes;
    el.className = bytes > 222 ? "bad" : bytes > 200 ? "near" : "";
    $("sendButton").disabled = bytes === 0 || bytes > 222;
    return bytes;
}

function authHeaders(){
    const headers = {"Content-Type":"application/json"};
    if(authRequired){
        const token = $("token").value.trim();
        if(token) headers["X-Crisis-Token"] = token;
    }
    return headers;
}

function formatUptime(seconds){
    seconds = Math.max(0, Number(seconds || 0));
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    if(h) return `${h}h ${m}m`;
    if(m) return `${m}m ${s}s`;
    return `${s}s`;
}

function formatTime(ts){
    if(!ts) return "-";
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString();
}

function renderHistory(items){
    const log = $("log");
    if(!items || !items.length){
        log.innerHTML = '<span class="empty">Brak transmisji w tej sesji serwera.</span>';
        return;
    }
    log.innerHTML = items.map(item => `
        <div class="logrow">
            <span class="time">[${escapeHtml(formatTime(item.timestamp))}]</span>
            <span class="tx">&gt; TX</span>
            <span class="txid">${escapeHtml(item.id)}</span>
            <span>${escapeHtml(item.message)}</span>
        </div>
    `).join("");
    lastMessage = items[0].message || lastMessage;
}

async function refreshStatus(){
    try{
        const r = await fetch("/api/status", {cache:"no-store"});
        const data = await r.json();

        authRequired = !!data.authRequired;
        $("authBox").style.display = authRequired ? "block" : "none";

        if(data.serialConnected){
            $("serialDot").className = "dot online";
            $("serialText").textContent = "OPEN";
            $("headerStatus").textContent = "UART READY";
        }else{
            $("serialDot").className = "dot";
            $("serialText").textContent = "CLOSED";
            $("headerStatus").textContent = "UART OFFLINE";
        }

        $("serialDevice").textContent = data.serialDevice || "-";
        $("baudRate").textContent = data.baudRate || "-";
        $("uptime").textContent = formatUptime(data.uptimeSeconds);
        $("lastTx").textContent = formatTime(data.lastTx);
        $("sentCount").textContent = data.txPackets ?? 0;
    }catch(e){
        $("serialDot").className = "dot warn";
        $("serialText").textContent = "BACKEND ERROR";
        $("headerStatus").textContent = "BACKEND ERROR";
    }
}

async function refreshHistory(){
    try{
        const r = await fetch("/api/history", {cache:"no-store"});
        const data = await r.json();
        renderHistory(data.items || []);
    }catch(e){
        // Status is still shown separately; history failure is non-critical.
    }
}

async function sendMessage(){
    const message = $("message").value.trim();
    const bytes = encoder.encode(message).length;

    if(!message){
        $("sendStatus").textContent = "ERROR: EMPTY MESSAGE";
        $("sendStatus").style.color = "var(--red2)";
        return;
    }

    if(bytes > 222){
        $("sendStatus").textContent = `ERROR: ${bytes}/222 BYTES`;
        $("sendStatus").style.color = "var(--red2)";
        return;
    }

    if(authRequired && !$("token").value.trim()){
        $("sendStatus").textContent = "ERROR: ACCESS TOKEN REQUIRED";
        $("sendStatus").style.color = "var(--red2)";
        return;
    }

    $("sendButton").disabled = true;
    $("sendStatus").textContent = "TRANSMITTING...";
    $("sendStatus").style.color = "var(--orange)";

    try{
        const r = await fetch("/api/messages", {
            method:"POST",
            headers:authHeaders(),
            body:JSON.stringify({message})
        });

        const data = await r.json();
        if(!r.ok || !data.ok){
            throw new Error(data.error || `HTTP ${r.status}`);
        }

        lastMessage = message;
        $("sendStatus").textContent = `TRANSMITTED [${data.id}] • ${data.bytes} B`;
        $("sendStatus").style.color = "var(--green)";
        $("message").value = "";
        updateByteCount();
        await Promise.all([refreshStatus(), refreshHistory()]);
    }catch(e){
        $("sendStatus").textContent = `ERROR: ${e.message}`;
        $("sendStatus").style.color = "var(--red2)";
        updateByteCount();
    }
}

$("message").addEventListener("input", updateByteCount);
$("sendButton").addEventListener("click", sendMessage);

$("clearButton").addEventListener("click", () => {
    $("message").value = "";
    $("message").focus();
    $("sendStatus").textContent = "";
    updateByteCount();
});

$("repeatButton").addEventListener("click", () => {
    if(!lastMessage){
        $("sendStatus").textContent = "NO PREVIOUS MESSAGE";
        $("sendStatus").style.color = "var(--orange)";
        return;
    }
    $("message").value = lastMessage;
    $("message").focus();
    updateByteCount();
});

document.querySelectorAll("[data-template]").forEach(btn => {
    btn.addEventListener("click", () => {
        $("message").value = btn.dataset.template || "";
        $("message").focus();
        $("message").setSelectionRange($("message").value.length, $("message").value.length);
        updateByteCount();
    });
});

document.addEventListener("keydown", e => {
    if((e.ctrlKey || e.metaKey) && e.key === "Enter"){
        e.preventDefault();
        if(!$("sendButton").disabled) sendMessage();
    }
});

$("token").addEventListener("input", () => {
    sessionStorage.setItem("crisisToken", $("token").value);
});

$("token").value = sessionStorage.getItem("crisisToken") || "";
updateByteCount();
refreshStatus();
refreshHistory();
setInterval(refreshStatus, 3000);
setInterval(refreshHistory, 5000);
</script>
</body>
</html>
"""


def open_serial():
    global ser

    if ser is not None and ser.is_open:
        return ser

    ser = serial.Serial(
        SERIAL_PORT,
        BAUD_RATE,
        timeout=1,
        write_timeout=2,
    )

    print(f"[UART] Connected: {SERIAL_PORT} @ {BAUD_RATE}")
    return ser


def close_serial():
    global ser
    try:
        if ser is not None:
            ser.close()
    except Exception:
        pass
    ser = None


def check_token():
    if not ACCESS_TOKEN:
        return True

    received = request.headers.get("X-Crisis-Token", "")
    return hmac.compare_digest(received, ACCESS_TOKEN)


def clean_message(value):
    return " ".join(str(value).replace("\r", " ").replace("\n", " ").split()).strip()


@app.get("/")
def index():
    return Response(PAGE, mimetype="text/html")


@app.get("/api/status")
def status():
    global last_error

    try:
        with serial_lock:
            port = open_serial()
            connected = bool(port.is_open)
        current_error = None
    except Exception as error:
        current_error = str(error)
        connected = False
        with metrics_lock:
            last_error = current_error
        print("[UART ERROR]", error)

    with metrics_lock:
        snapshot = {
            "txPackets": tx_packets,
            "lastTx": last_tx_at,
            "lastError": last_error,
        }

    return jsonify({
        "serialConnected": connected,
        "serialDevice": SERIAL_PORT,
        "baudRate": BAUD_RATE,
        "maxMessageBytes": MAX_MESSAGE_BYTES,
        "uptimeSeconds": int(time.time() - started_at),
        "authRequired": bool(ACCESS_TOKEN),
        **snapshot,
    })


@app.get("/api/history")
def get_history():
    with metrics_lock:
        items = list(history)
    return jsonify({"items": items})


@app.post("/api/messages")
def send_message():
    global ser, tx_packets, last_tx_at, last_error

    if not check_token():
        return jsonify({
            "ok": False,
            "error": "Unauthorized",
        }), 401

    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        return jsonify({
            "ok": False,
            "error": "JSON body required",
        }), 400

    message = clean_message(data.get("message", ""))

    if not message:
        return jsonify({
            "ok": False,
            "error": "Empty message",
        }), 400

    raw = message.encode("utf-8")

    if len(raw) > MAX_MESSAGE_BYTES:
        return jsonify({
            "ok": False,
            "error": f"Message longer than {MAX_MESSAGE_BYTES} bytes",
            "bytes": len(raw),
        }), 400

    try:
        with serial_lock:
            port = open_serial()
            port.write(raw + b"\n")
            port.flush()

        now = time.time()
        tx_id = f"TX-{int(now * 1000)}"

        with metrics_lock:
            tx_packets += 1
            last_tx_at = now
            last_error = None
            history.appendleft({
                "id": tx_id,
                "timestamp": now,
                "message": message,
                "bytes": len(raw),
            })

        print(f"[UART TX] {tx_id} | {len(raw)} B | {message}")

        return jsonify({
            "ok": True,
            "id": tx_id,
            "bytes": len(raw),
        })

    except Exception as error:
        print("[UART ERROR]", error)

        with serial_lock:
            close_serial()

        with metrics_lock:
            last_error = str(error)

        return jsonify({
            "ok": False,
            "error": str(error),
        }), 500


if __name__ == "__main__":
    print("=== CRISIS MESH ===")
    print(f"UART: {SERIAL_PORT} @ {BAUD_RATE}")
    print(f"PAYLOAD: text only, max {MAX_MESSAGE_BYTES} bytes UTF-8")
    print(f"AUTH: {'token enabled' if ACCESS_TOKEN else 'disabled'}")
    print("WEB: http://0.0.0.0:8080")

    app.run(
        host="0.0.0.0",
        port=8080,
        debug=False,
        threaded=True,
        use_reloader=False,
    )
