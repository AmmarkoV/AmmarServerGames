/* FRISK voice clips - file input everywhere, hold-to-record in secure contexts. */
(function () {
"use strict";

const MAX_BYTES = 512 * 1024;
const MAX_RECORD_MS = 15000;

function button(text, className) {
  const node = document.createElement("button");
  node.type = "button";
  node.className = className || "secondary tiny";
  node.textContent = text;
  return node;
}

function recorderMime() {
  const choices = [
    "audio/webm;codecs=opus", "audio/ogg;codecs=opus",
    "audio/mp4", "audio/webm", "audio/ogg"
  ];
  if (typeof MediaRecorder === "undefined" ||
      typeof MediaRecorder.isTypeSupported !== "function") {
    return "";
  }
  for (const type of choices) {
    if (MediaRecorder.isTypeSupported(type)) return type;
  }
  return "";
}

function extensionFor(type, fallback) {
  if (/ogg/i.test(type)) return "ogg";
  if (/mp4|m4a|aac/i.test(type)) return "m4a";
  if (/webm/i.test(type)) return "webm";
  if (/mpeg|mp3/i.test(type)) return "mp3";
  return fallback || "webm";
}

function mount(container, options) {
  if (!container) return null;
  const opts = options || {};
  const canRecord = !!(window.isSecureContext && navigator.mediaDevices &&
    typeof navigator.mediaDevices.getUserMedia === "function" &&
    typeof MediaRecorder !== "undefined");

  const status = document.createElement("p");
  status.className = "small muted voice-status";
  const controls = document.createElement("div");
  controls.className = "voice-controls";
  const mic = button(canRecord ? "🎤 Hold to talk" : "🎤 Record clip",
                     "secondary tiny voice-mic");
  mic.title = canRecord
    ? "Hold to record up to 15 seconds, release to preview"
    : "Record with the phone's voice recorder or choose an audio file";
  const upload = button("Upload clip", "secondary tiny voice-upload");
  const fileInput = document.createElement("input");
  fileInput.type = "file";
  fileInput.accept = "audio/*";
  fileInput.setAttribute("capture", "");
  fileInput.className = "hidden";
  const level = document.createElement("div");
  level.className = "voice-level hidden";
  const levelFill = document.createElement("div");
  levelFill.className = "voice-level-fill";
  level.append(levelFill);
  const preview = document.createElement("div");
  preview.className = "voice-preview hidden";
  controls.append(mic, upload, fileInput, level);
  container.append(controls, status, preview);

  const state = {
    enabled: true,
    recording: false,
    starting: false,
    wantStop: false,
    recorder: null,
    stream: null,
    chunks: [],
    discard: false,
    startedAt: 0,
    timer: null,
    blob: null,
    name: "",
    durationMs: 0,
    url: "",
    audioContext: null,
    analyser: null,
    source: null,
    raf: 0
  };

  function say(message, bad) {
    status.textContent = message || "";
    status.classList.toggle("bad", !!bad);
  }

  function fail(message) {
    say(message, true);
    if (opts.onError) opts.onError(message);
  }

  function setMeter(value) {
    levelFill.style.width = `${Math.max(0, Math.min(1, value)) * 100}%`;
  }

  function stopMeter() {
    if (state.raf) cancelAnimationFrame(state.raf);
    state.raf = 0;
    if (state.source) {
      try { state.source.disconnect(); } catch (error) { /* closed */ }
      state.source = null;
    }
    if (state.analyser) {
      try { state.analyser.disconnect(); } catch (error) { /* closed */ }
      state.analyser = null;
    }
    if (state.audioContext) {
      try { state.audioContext.close(); } catch (error) { /* closed */ }
      state.audioContext = null;
    }
    level.classList.add("hidden");
    setMeter(0);
  }

  function meter() {
    if (!state.analyser) return;
    const data = new Uint8Array(state.analyser.fftSize);
    state.analyser.getByteTimeDomainData(data);
    let peak = 0;
    for (const sample of data) {
      peak = Math.max(peak, Math.abs(sample - 128) / 128);
    }
    setMeter(peak);
    state.raf = requestAnimationFrame(meter);
  }

  function stopStream() {
    if (state.stream) {
      for (const track of state.stream.getTracks()) track.stop();
      state.stream = null;
    }
    stopMeter();
  }

  function clearTimer() {
    if (state.timer) clearTimeout(state.timer);
    state.timer = null;
  }

  function resetPreview() {
    if (state.url) URL.revokeObjectURL(state.url);
    state.url = "";
    state.blob = null;
    state.name = "";
    state.durationMs = 0;
    preview.replaceChildren();
    preview.classList.add("hidden");
  }

  function showPreview(blob, name, durationMs) {
    resetPreview();
    if (!state.enabled) return;
    if (blob.size > MAX_BYTES) {
      fail("That clip is larger than 512 KB.");
      return;
    }
    state.blob = blob;
    state.name = name;
    state.durationMs = Math.max(0, Math.round(durationMs || 0));
    state.url = URL.createObjectURL(blob);

    const audio = document.createElement("audio");
    audio.controls = true;
    audio.preload = "metadata";
    audio.src = state.url;
    audio.addEventListener("loadedmetadata", () => {
      if (Number.isFinite(audio.duration) && audio.duration > 0) {
        state.durationMs = Math.round(audio.duration * 1000);
      }
    });
    audio.addEventListener("error", () => {
      say("This browser may not preview that recording; it can still be sent.",
          true);
    });

    const send = button("Send clip", "tiny voice-send");
    const discard = button("Discard", "secondary tiny");
    const row = document.createElement("div");
    row.className = "voice-preview-actions";
    row.append(send, discard);
    preview.append(audio, row);
    preview.classList.remove("hidden");
    say("Preview the clip, then send or discard it.");

    send.addEventListener("click", async () => {
      if (!state.blob) return;
      send.disabled = true;
      discard.disabled = true;
      try {
        const result = opts.onClip
          ? await opts.onClip(state.blob, state.name, state.durationMs)
          : false;
        if (result === false) {
          send.disabled = false;
          discard.disabled = false;
          return;
        }
        resetPreview();
        say("Clip sent.");
      } catch (error) {
        send.disabled = false;
        discard.disabled = false;
        fail("Could not send the clip.");
      }
    });
    discard.addEventListener("click", () => {
      resetPreview();
      say("");
    });
  }

  function finishRecording() {
    clearTimer();
    stopStream();
    mic.classList.remove("recording");
    mic.textContent = canRecord ? "🎤 Hold to talk" : "🎤 Record clip";
    state.recording = false;
    state.starting = false;
    const type = state.recorder && state.recorder.mimeType
      ? state.recorder.mimeType : "audio/webm";
    const blob = new Blob(state.chunks, { type });
    const discard = state.discard;
    state.chunks = [];
    state.recorder = null;
    state.discard = false;
    if (discard || blob.size === 0) {
      say("");
      return;
    }
    const duration = state.startedAt ? performance.now() - state.startedAt : 0;
    showPreview(blob, `clip.${extensionFor(type)}`, duration);
  }

  function stopRecording(discard) {
    state.wantStop = true;
    if (discard) state.discard = true;
    if (state.recorder && state.recorder.state !== "inactive") {
      try { state.recorder.stop(); } catch (error) { finishRecording(); }
    } else if (!state.starting) {
      finishRecording();
    }
  }

  async function startRecording() {
    if (!state.enabled || !canRecord || state.recording || state.starting) {
      return;
    }
    resetPreview();
    state.starting = true;
    state.wantStop = false;
    state.discard = false;
    state.chunks = [];
    say("Starting the microphone…");
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: true, noiseSuppression: true }
      });
      if (state.wantStop || !state.enabled) {
        for (const track of stream.getTracks()) track.stop();
        state.starting = false;
        say("");
        return;
      }
      state.stream = stream;
      const type = recorderMime();
      const recorder = type ? new MediaRecorder(stream, { mimeType: type })
        : new MediaRecorder(stream);
      state.recorder = recorder;
      state.startedAt = performance.now();
      recorder.addEventListener("dataavailable", (event) => {
        if (event.data && event.data.size > 0) state.chunks.push(event.data);
      });
      recorder.addEventListener("stop", finishRecording);
      recorder.addEventListener("error", () => {
        fail("The recorder stopped unexpectedly.");
        stopRecording(true);
      });
      recorder.start(250);
      state.recording = true;
      state.starting = false;
      mic.classList.add("recording");
      mic.textContent = "● Release to stop";
      level.classList.remove("hidden");
      say("Recording… release to preview.");
      state.timer = setTimeout(() => stopRecording(false), MAX_RECORD_MS);
      try {
        const AudioContext = window.AudioContext || window.webkitAudioContext;
        if (AudioContext) {
          state.audioContext = new AudioContext();
          if (state.audioContext.state === "suspended") {
            state.audioContext.resume().catch(() => {});
          }
          state.source = state.audioContext.createMediaStreamSource(stream);
          state.analyser = state.audioContext.createAnalyser();
          state.analyser.fftSize = 512;
          state.source.connect(state.analyser);
          meter();
        }
      } catch (error) {
        /* the meter is optional */
      }
    } catch (error) {
      state.starting = false;
      state.wantStop = false;
      stopStream();
      say("Microphone unavailable; choose an audio file instead.", true);
      if (opts.onError) opts.onError("Microphone unavailable.");
    }
  }

  function openFilePicker() {
    if (!state.enabled) return;
    resetPreview();
    fileInput.value = "";
    fileInput.click();
  }

  fileInput.addEventListener("change", () => {
    const file = fileInput.files && fileInput.files[0];
    if (!file) return;
    if (file.size > MAX_BYTES) {
      fail("That clip is larger than 512 KB.");
      return;
    }
    showPreview(file, file.name || `clip.${extensionFor(file.type)}`, 0);
  });

  if (canRecord) {
    mic.addEventListener("pointerdown", (event) => {
      event.preventDefault();
      try { mic.setPointerCapture(event.pointerId); }
      catch (error) { /* not all browsers */ }
      startRecording();
    });
    const release = (event) => {
      if (event) event.preventDefault();
      if (state.recording || state.starting) stopRecording(false);
    };
    mic.addEventListener("pointerup", release);
    mic.addEventListener("pointercancel", () => {
      if (state.recording || state.starting) stopRecording(true);
    });
    mic.addEventListener("lostpointercapture", () => {
      if (state.recording) stopRecording(false);
    });
    mic.addEventListener("keydown", (event) => {
      if (event.key === " " || event.key === "Enter") {
        event.preventDefault();
        startRecording();
      }
    });
    mic.addEventListener("keyup", (event) => {
      if (event.key === " " || event.key === "Enter") {
        event.preventDefault();
        stopRecording(false);
      }
    });
  } else {
    mic.addEventListener("click", openFilePicker);
  }
  upload.addEventListener("click", openFilePicker);

  function reset() {
    clearTimer();
    state.wantStop = true;
    state.discard = true;
    if (state.recorder && state.recorder.state !== "inactive") {
      try { state.recorder.stop(); } catch (error) { finishRecording(); }
    }
    stopStream();
    state.recording = false;
    state.starting = false;
    state.chunks = [];
    mic.classList.remove("recording");
    mic.textContent = canRecord ? "🎤 Hold to talk" : "🎤 Record clip";
    resetPreview();
    say("");
  }

  function setEnabled(enabled) {
    state.enabled = !!enabled;
    mic.disabled = !state.enabled;
    upload.disabled = !state.enabled;
    if (!state.enabled) reset();
  }

  return { reset, setEnabled };
}

window.FriskVoice = { mount };
}());
