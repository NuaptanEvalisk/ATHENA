import GstWebRTCAPI from "./vendor/gstwebrtc-api/gstwebrtc-api.js";

const elements = {
  video: document.querySelector("#desktop-video"),
  stage: document.querySelector("#desktop-stage"),
  startup: document.querySelector("#startup-panel"),
  connection: document.querySelector("#connection-label"),
  timer: document.querySelector("#timer"),
  extend: document.querySelector("#extend-button"),
  upload: document.querySelector("#upload-button"),
  downloads: document.querySelector("#downloads-button"),
  fullscreen: document.querySelector("#fullscreen-button"),
  fileInput: document.querySelector("#file-input"),
  dropOverlay: document.querySelector("#drop-overlay"),
  downloadsPanel: document.querySelector("#downloads-panel"),
  closeDownloads: document.querySelector("#close-downloads"),
  refreshDownloads: document.querySelector("#refresh-downloads"),
  downloadAll: document.querySelector("#download-all"),
  downloadsEmpty: document.querySelector("#downloads-empty"),
  downloadsList: document.querySelector("#downloads-list"),
  messageScreen: document.querySelector("#message-screen"),
  messageTitle: document.querySelector("#message-title"),
  messageText: document.querySelector("#message-text"),
  retry: document.querySelector("#retry-button"),
  expiredDownload: document.querySelector("#expired-download-button"),
  toastRegion: document.querySelector("#toast-region"),
};

let session = null;
let webrtcApi = null;
let consumerSession = null;
let pollTimer = null;
let heartbeatTimer = null;
let countdownTimer = null;
let dragDepth = 0;
let closing = false;

function apiUrl(suffix = "") {
  if (!session?.token) {
    throw new Error("No active session");
  }
  return `/api/sessions/${session.token}${suffix}`;
}

async function jsonRequest(url, options = {}) {
  const response = await fetch(url, {
    cache: "no-store",
    ...options,
  });
  let body = {};
  try {
    body = await response.json();
  } catch {
    body = {};
  }
  if (!response.ok) {
    const error = new Error(body.error || `Request failed (${response.status})`);
    error.status = response.status;
    throw error;
  }
  return body;
}

function showToast(message, duration = 4500) {
  const toast = document.createElement("div");
  toast.className = "toast";
  toast.textContent = message;
  elements.toastRegion.append(toast);
  window.setTimeout(() => toast.remove(), duration);
}

function showMessage(title, text, { retry = true, expired = false } = {}) {
  elements.messageTitle.textContent = title;
  elements.messageText.textContent = text;
  elements.retry.hidden = !retry;
  elements.expiredDownload.hidden = !expired;
  elements.messageScreen.hidden = false;
}

function clearTimers() {
  for (const timer of [pollTimer, heartbeatTimer, countdownTimer]) {
    if (timer !== null) {
      window.clearInterval(timer);
      window.clearTimeout(timer);
    }
  }
  pollTimer = null;
  heartbeatTimer = null;
  countdownTimer = null;
}

function disconnectWebRTC() {
  if (consumerSession) {
    consumerSession.close();
    consumerSession = null;
  }
  if (webrtcApi) {
    // gstwebrtc-api has no public API-level close operation. Stop its
    // reconnect loop before closing the owned signaling channel.
    webrtcApi._config.reconnectionTimeout = 0;
    webrtcApi._channel?.close();
    webrtcApi = null;
  }
  elements.video.srcObject = null;
}

function updateCountdown() {
  if (!session?.expires_at) {
    elements.timer.textContent = "--:--";
    return;
  }
  const remaining = Math.max(
    0,
    Math.ceil(session.expires_at - Date.now() / 1000),
  );
  const hours = Math.floor(remaining / 3600);
  const minutes = Math.floor((remaining % 3600) / 60);
  const seconds = remaining % 60;
  elements.timer.textContent = hours > 0
    ? `${hours}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`
    : `${minutes}:${String(seconds).padStart(2, "0")}`;
  elements.extend.hidden = remaining > (session.warning_seconds || 300);
}

function sessionWebSocketUrl() {
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  return `${protocol}//${location.host}${apiUrl("/signal")}`;
}

function attachConsumer(producer) {
  if (consumerSession) {
    return;
  }
  const next = webrtcApi.createConsumerSession(producer.id);
  if (!next) {
    showMessage(
      "Unable to connect to ATHENA",
      "The remote desktop producer could not be opened.",
    );
    return;
  }
  consumerSession = next;
  next.addEventListener("error", (event) => {
    console.error("ATHENA WebRTC consumer error", event.message, event.error);
    elements.connection.textContent = "Desktop connection interrupted";
  });
  next.addEventListener("closed", () => {
    if (consumerSession === next) {
      consumerSession = null;
      elements.connection.textContent = "Desktop connection interrupted";
    }
  });
  next.addEventListener("streamsChanged", () => {
    if (consumerSession !== next || next.streams.length === 0) {
      return;
    }
    elements.video.srcObject = next.streams[0];
    elements.video.play().catch(() => {});
  });
  next.addEventListener("remoteControllerChanged", () => {
    if (consumerSession !== next || !next.remoteController) {
      return;
    }
    next.remoteController.attachVideoElement(elements.video);
    elements.video.focus();
  });
  next.connect();
}

function connectWebRTC() {
  if (webrtcApi) {
    return;
  }
  const api = new GstWebRTCAPI({
    meta: { name: "ATHENA-Web-Client" },
    signalingServerUrl: sessionWebSocketUrl(),
    reconnectionTimeout: 1500,
    webrtcConfig: {
      iceServers: session.ice_servers || [],
      bundlePolicy: "max-bundle",
    },
  });
  webrtcApi = api;
  const peers = {
    producerAdded: attachConsumer,
    producerRemoved(producer) {
      if (consumerSession?.peerId === producer.id) {
        consumerSession.close();
      }
    },
  };
  api.registerPeerListener(peers);
  api.registerConnectionListener({
    connected() {
      elements.connection.textContent = "Connecting desktop";
      for (const producer of api.getAvailableProducers()) {
        attachConsumer(producer);
      }
    },
    disconnected() {
      elements.connection.textContent = "Reconnecting desktop";
    },
  });
}

function applySessionStatus(state) {
  session = { ...session, ...state };
  updateCountdown();
  if (state.status === "starting") {
    elements.connection.textContent = "Preparing private workspace";
    return;
  }
  if (state.status === "running") {
    elements.connection.textContent = "Private session";
    connectWebRTC();
    return;
  }
  clearTimers();
  disconnectWebRTC();
  if (state.status === "expired") {
    showMessage(
      "Session ended",
      "This temporary workspace reached its time limit. You can still retrieve everything that was placed in Desktop/Download.",
      { retry: true, expired: Boolean(state.can_download_expired) },
    );
  } else {
    showMessage(
      "ATHENA could not start",
      state.error || "The isolated workspace ended unexpectedly.",
    );
  }
}

async function pollStatus() {
  try {
    applySessionStatus(await jsonRequest(apiUrl()));
  } catch (error) {
    if (error.status === 404) {
      clearTimers();
      disconnectWebRTC();
      showMessage(
        "Session is no longer available",
        "The temporary workspace has already been removed.",
      );
    }
  }
}

function startLifecycleTimers() {
  pollTimer = window.setInterval(pollStatus, 1000);
  heartbeatTimer = window.setInterval(async () => {
    try {
      applySessionStatus(await jsonRequest(apiUrl("/heartbeat"), {
        method: "POST",
      }));
    } catch (error) {
      console.warn("ATHENA session heartbeat failed", error);
    }
  }, 15000);
  countdownTimer = window.setInterval(updateCountdown, 1000);
}

async function createSession() {
  clearTimers();
  disconnectWebRTC();
  session = null;
  closing = false;
  elements.messageScreen.hidden = true;
  elements.startup.hidden = false;
  elements.connection.textContent = "Preparing private workspace";
  elements.timer.textContent = "--:--";
  try {
    const state = await jsonRequest("/api/sessions", { method: "POST" });
    applySessionStatus(state);
    startLifecycleTimers();
  } catch (error) {
    elements.startup.hidden = true;
    if (error.status === 503) {
      showMessage(
        "All ATHENA sessions are in use",
        "The demonstration service is at capacity. Please try again later.",
      );
    } else {
      showMessage("ATHENA is unavailable", error.message);
    }
  }
}

async function extendSession() {
  try {
    applySessionStatus(await jsonRequest(apiUrl("/extend"), {
      method: "POST",
    }));
    showToast("Session extended by 60 minutes.");
  } catch (error) {
    showToast(error.message);
  }
}

function formatSize(bytes) {
  const units = ["B", "KiB", "MiB", "GiB"];
  let value = Number(bytes);
  let unit = 0;
  while (value >= 1024 && unit + 1 < units.length) {
    value /= 1024;
    unit += 1;
  }
  return `${value < 10 && unit > 0 ? value.toFixed(1) : Math.round(value)} ${units[unit]}`;
}

async function refreshDownloads() {
  try {
    const result = await jsonRequest(apiUrl("/downloads"));
    elements.downloadsList.replaceChildren();
    elements.downloadsEmpty.hidden = result.files.length !== 0;
    for (const file of result.files) {
      const item = document.createElement("li");
      const link = document.createElement("a");
      link.href = apiUrl(`/download/${encodeURIComponent(file.path)}`);
      link.textContent = file.path;
      link.download = file.path.split("/").at(-1);
      const size = document.createElement("span");
      size.textContent = formatSize(file.size);
      item.append(link, size);
      elements.downloadsList.append(item);
    }
  } catch (error) {
    showToast(error.message);
  }
}

async function uploadFiles(files) {
  const queue = Array.from(files);
  if (queue.length === 0) {
    return;
  }
  for (const file of queue) {
    try {
      const response = await fetch(apiUrl("/upload"), {
        method: "PUT",
        headers: {
          "X-ATHENA-Filename": encodeURIComponent(file.name),
          "Content-Type": "application/octet-stream",
        },
        body: file,
      });
      const result = await response.json();
      if (!response.ok) {
        throw new Error(result.error || `Upload failed (${response.status})`);
      }
      showToast(`Uploaded ${file.name}`);
    } catch (error) {
      showToast(`${file.name}: ${error.message}`, 7000);
    }
  }
}

function closeSessionBestEffort() {
  if (closing || !session?.token) {
    return;
  }
  closing = true;
  const url = apiUrl("/close");
  if (!navigator.sendBeacon(url, new Blob([], { type: "text/plain" }))) {
    fetch(url, { method: "POST", keepalive: true }).catch(() => {});
  }
}

elements.video.addEventListener("playing", () => {
  elements.startup.hidden = true;
  elements.connection.textContent = "Private session";
  elements.video.focus();
});
elements.extend.addEventListener("click", extendSession);
elements.upload.addEventListener("click", () => elements.fileInput.click());
elements.fileInput.addEventListener("change", () => {
  uploadFiles(elements.fileInput.files);
  elements.fileInput.value = "";
});
elements.downloads.addEventListener("click", async () => {
  elements.downloadsPanel.hidden = !elements.downloadsPanel.hidden;
  if (!elements.downloadsPanel.hidden) {
    await refreshDownloads();
  }
});
elements.closeDownloads.addEventListener("click", () => {
  elements.downloadsPanel.hidden = true;
  elements.video.focus();
});
elements.refreshDownloads.addEventListener("click", refreshDownloads);
elements.downloadAll.addEventListener("click", () => {
  location.href = apiUrl("/download-all");
});
elements.expiredDownload.addEventListener("click", () => {
  location.href = apiUrl("/download-all");
});
elements.retry.addEventListener("click", createSession);
elements.fullscreen.addEventListener("click", () => {
  elements.stage.requestFullscreen().catch((error) => showToast(error.message));
});

for (const name of ["dragenter", "dragover"]) {
  window.addEventListener(name, (event) => {
    event.preventDefault();
    if (name === "dragenter") {
      dragDepth += 1;
    }
    elements.dropOverlay.hidden = false;
  });
}
window.addEventListener("dragleave", (event) => {
  event.preventDefault();
  dragDepth = Math.max(0, dragDepth - 1);
  if (dragDepth === 0) {
    elements.dropOverlay.hidden = true;
  }
});
window.addEventListener("drop", (event) => {
  event.preventDefault();
  dragDepth = 0;
  elements.dropOverlay.hidden = true;
  uploadFiles(event.dataTransfer.files);
});
window.addEventListener("pagehide", closeSessionBestEffort);

createSession();
