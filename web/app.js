const $ = (id) => document.getElementById(id);

const state = {
    sent: 0,
    ack: 0,
    messages: []
};

const messageInput = $("message");
const charCount = $("charCount");
const sendStatus = $("sendStatus");
const sendButton = $("sendButton");
const eventLog = $("eventLog");
const history = $("messageHistory");

messageInput.addEventListener("input", () => {
    charCount.textContent = messageInput.value.length;
});

$("messageForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const payload = {
        type: $("type").value,
        priority: Number($("priority").value),
        title: $("title").value.trim(),
        message: $("message").value.trim()
    };

    if (!payload.message) {
        setSendStatus("Wpisz treść komunikatu.", false);
        return;
    }

    sendButton.disabled = true;
    setSendStatus("Wysyłanie do gateway...", true);

    try {
        const response = await fetch("/api/messages", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(payload)
        });

        const result = await response.json();

        if (!response.ok || !result.ok) {
            throw new Error(result.error || `HTTP ${response.status}`);
        }

        state.sent += 1;
        $("sentCount").textContent = state.sent;

        state.messages.unshift({
            ...payload,
            id: result.id,
            time: new Date()
        });

        renderHistory();
        setSendStatus(`Wysłano: ${shortId(result.id)}`, true);

        $("message").value = "";
        charCount.textContent = "0";
    } catch (error) {
        setSendStatus(error.message, false);
    } finally {
        sendButton.disabled = false;
    }
});

$("clearEvents").addEventListener("click", () => {
    eventLog.innerHTML = `
        <div class="empty-state">
            Oczekiwanie na zdarzenia z ESP32...
        </div>
    `;
});

function setSendStatus(text, ok) {
    sendStatus.textContent = text;
    sendStatus.style.color = ok ? "var(--green)" : "var(--accent-2)";
}

function priorityLabel(priority) {
    if (priority === 3) return "KRYTYCZNY";
    if (priority === 2) return "WYSOKI";
    return "NISKI";
}

function shortId(id) {
    if (!id) return "—";
    return id.length > 12 ? `${id.slice(0, 12)}…` : id;
}

function renderHistory() {
    if (!state.messages.length) {
        history.innerHTML =
            `<div class="empty-state">Nie wysłano jeszcze żadnego komunikatu.</div>`;
        return;
    }

    history.innerHTML = state.messages.slice(0, 12).map((item) => `
        <div class="message-row">
            <div class="message-id" title="${escapeHtml(item.id)}">
                ${escapeHtml(shortId(item.id))}
            </div>
            <div class="message-title">
                ${escapeHtml(item.title || item.type)}
            </div>
            <div class="message-text" title="${escapeHtml(item.message)}">
                ${escapeHtml(item.message)}
            </div>
            <div class="priority p${item.priority}">
                ${priorityLabel(item.priority)}
            </div>
        </div>
    `).join("");
}

function addEvent(event) {
    const empty = eventLog.querySelector(".empty-state");
    if (empty) empty.remove();

    const row = document.createElement("div");
    row.className = "event";

    const type = event.type || "EVENT";
    const time = new Date(event.receivedAt || event.timestamp || Date.now());

    row.innerHTML = `
        <div class="event-top">
            <span class="event-type">${escapeHtml(type)}</span>
            <span class="event-time">${time.toLocaleTimeString()}</span>
        </div>
        <div class="event-body">${escapeHtml(JSON.stringify(event))}</div>
    `;

    eventLog.prepend(row);

    while (eventLog.children.length > 50) {
        eventLog.removeChild(eventLog.lastChild);
    }

    if (String(type).toUpperCase().includes("ACK")) {
        state.ack += 1;
        $("ackCount").textContent = state.ack;
    }
}

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}

async function refreshStatus() {
    try {
        const response = await fetch("/api/status");
        const data = await response.json();

        const ok = Boolean(data.serialConnected);

        $("serialDot").className = `status-dot ${ok ? "ok" : "bad"}`;
        $("serialText").textContent = ok ? "Gateway online" : "Gateway offline";
        $("serialDevice").textContent = data.serialDevice || "—";
        $("gatewayStat").textContent = ok ? "ONLINE" : "OFFLINE";
    } catch {
        $("serialDot").className = "status-dot bad";
        $("serialText").textContent = "Backend offline";
        $("gatewayStat").textContent = "OFFLINE";
    }
}

function connectWebSocket() {
    const scheme = location.protocol === "https:" ? "wss" : "ws";
    const socket = new WebSocket(`${scheme}://${location.host}/ws`);

    socket.addEventListener("open", () => {
        $("wsDot").className = "status-dot ok";
        $("wsText").textContent = "Live";
    });

    socket.addEventListener("message", (message) => {
        try {
            addEvent(JSON.parse(message.data));
        } catch {
            addEvent({
                type: "WS_TEXT",
                line: message.data,
                receivedAt: Date.now()
            });
        }
    });

    socket.addEventListener("close", () => {
        $("wsDot").className = "status-dot bad";
        $("wsText").textContent = "Rozłączono";

        // Automatic reconnect.
        setTimeout(connectWebSocket, 1500);
    });

    socket.addEventListener("error", () => {
        socket.close();
    });
}

refreshStatus();
setInterval(refreshStatus, 3000);
connectWebSocket();