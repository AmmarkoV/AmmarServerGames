/* FRISK browser client - dependency free, one file for the playable core and async comfort. */
(() => {
"use strict";

const $ = (id) => document.getElementById(id);
const NS = "http://www.w3.org/2000/svg";
const MAP_W = 1088;
const MAP_H = 1600;

const state = {
  me: null,
  csrf: "",
  viewMode: "auth",
  lobby: null,
  lobbySince: 0,
  lobbyAt: 0,
  gameId: null,
  game: null,
  gameAt: 0,
  mapMeta: null,
  mapPack: null,
  mapWidth: MAP_W,
  mapHeight: MAP_H,
  svg: null,
  badges: null,
  selectedFrom: null,
  selectedTo: null,
  pendingPlace: null,
  pendingLeader: null,
  bringLeader: false,
  blitzStop: 1,
  cardPlay: null,   /* printed power waiting for map picks: {slot, effect, from} */
  viewFitted: true,
  hover: null,
  pieceSymbols: null,
  view: { scale: 1, x: 0, y: 0 },
  pointers: new Map(),
  drag: null,
  pinch: null,
  suppressClick: false,
  loadingGame: false,
  requestSeq: 0,
  offline: false,
  replayChecked: false,
  replayActive: false,
  replayEvents: [],
  replayIndex: 0,
  replaySpeed: 1,
  replayTimer: null,
  replayFrom: null,
  replayTo: null,
  notifiedTurn: -1,
  notifiedGameKey: "",
  toastTimer: null
};

const errorText = {
  network: "Connection lost - retrying…",
  login: "Wrong username or password.",
  name: "Use 3-31 letters, digits, '_' or '-'.",
  password: "That password (or game password) is not valid.",
  exists: "That username is already taken.",
  invite: "That invite code is not usable.",
  csrf: "Session expired - reload the page.",
  rate: "Too many attempts. Wait a few minutes.",
  profile: "Could not load or save the profile.",
  colour: "Colour must look like #rrggbb.",
  glyph: "Glyph must be 1-15 printable characters.",
  limit: "You already have five outstanding invites.",
  not_implemented: "Not available yet.",
  post: "Invalid request.",
  overflow: "The server response overflowed.",
  full: "That game is full.",
  started: "That game has already started.",
  host: "Only the host can do that.",
  running: "A running game cannot be deleted here.",
  notfound: "That game does not exist any more.",
  seat: "You are not seated in this game.",
  faction: "That faction is already taken.",
  game: "Could not read or change the game.",
  params: "Some action fields are missing.",
  action: "That move is not allowed.",
  save: "The change could not be saved.",
  map: "That map pack is not available.",
  seats: "Choose 2 to 4 seats.",
  mode: "Choose a setup mode.",
  phase: "Kick votes are only available during a normal turn.",
  target: "Choose another living player.",
  idle: "That player has been active recently.",
  vote: "Another kick vote is already in progress.",
  voted: "You already voted to remove that player.",
  log: "Could not read the game log.",
  room: "That chat room does not exist.",
  text: "Messages must be 1-500 bytes of valid UTF-8 text.",
  taunt: "That taunt does not exist.",
  toolarge: "That clip is larger than 512 KB.",
  clip: "Choose an Ogg, WebM, WAV, M4A, MP3 or AMR audio clip.",
  save: "The message could not be saved.",
  admin: "Only the administrator can do that."
};

/* ---------------------------------------------------------------- */
/* Music                                                             */
/* ---------------------------------------------------------------- */

const BG_TRACKS = ["bg1.mp3", "bg2.mp3", "bg3.mp3", "bg4.mp3", "bg5.mp3", "bg6.mp3"];
const COMBAT_TRACKS = ["combat.mp3", "combat2.mp3", "combat3.mp3"];

const music = {
  audio: null,
  enabled: true,
  volume: 0.45,
  armed: false,
  mode: null,
  track: null,
  bgIndex: 0,
  combatIndex: 0,
  combatUntil: 0
};

try {
  if (localStorage.getItem("friskMusic") === "off") music.enabled = false;
  const storedVolume = localStorage.getItem("friskMusicVolume");
  const savedVolume = storedVolume === null ? NaN : Number(storedVolume);
  if (Number.isFinite(savedVolume) && savedVolume >= 0 && savedVolume <= 1) {
    music.volume = savedVolume;
  }
} catch (error) { /* storage unavailable */ }

/* ---------------------------------------------------------------- */
/* Dice, end-of-game banner and sound effects (PLAN.md 8.7, 8.12)    */
/* ---------------------------------------------------------------- */

const fx = {
  anim: true,
  sound: true,
  diceGame: null,
  diceVersion: 0,
  diceTimers: [],
  endDismissed: false,
  endSound: false,
  lastTap: null,
  lastPointer: "mouse",
  audio: null,
  chimes: 0,
  infoTimer: null,
  lobbyTurns: null
};

try {
  fx.anim = localStorage.getItem("friskDiceAnim") !== "off";
  fx.sound = localStorage.getItem("friskSfx") !== "off";
} catch (error) { /* storage unavailable */ }

function reducedMotion() {
  return typeof matchMedia === "function" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;
}

function playEffect(file) {
  if (!fx.sound) return;
  try {
    const audio = new Audio(`/sounds/${file}`);
    audio.volume = 0.8;
    const playing = audio.play();
    if (playing) playing.catch(() => { /* autoplay blocked */ });
  } catch (error) { /* audio unavailable */ }
}

/* "Your turn" chime.  Desktop notifications need HTTPS/localhost, so on a
 * plain-HTTP LAN this sound (plus the tab title) is the turn signal. */
function audioContext() {
  const Context = window.AudioContext || window.webkitAudioContext;
  if (!Context) return null;
  if (!fx.audio) fx.audio = new Context();
  if (fx.audio.state === "suspended") fx.audio.resume().catch(() => { /* needs a gesture */ });
  return fx.audio;
}

function unlockAudio() {
  try { audioContext(); } catch (error) { /* audio unavailable */ }
  document.removeEventListener("pointerdown", unlockAudio, true);
  document.removeEventListener("keydown", unlockAudio, true);
}
document.addEventListener("pointerdown", unlockAudio, true);
document.addEventListener("keydown", unlockAudio, true);

function playTurnChime() {
  if (!fx.sound) return;
  try {
    const ctx = audioContext();
    if (!ctx || ctx.state !== "running") return;
    const now = ctx.currentTime;
    for (const [frequency, at] of [[659.25, 0], [880, 0.15]]) {
      const tone = ctx.createOscillator();
      const gain = ctx.createGain();
      tone.type = "sine";
      tone.frequency.value = frequency;
      gain.gain.setValueAtTime(0.0001, now + at);
      gain.gain.exponentialRampToValueAtTime(0.22, now + at + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + at + 0.5);
      tone.connect(gain).connect(ctx.destination);
      tone.start(now + at);
      tone.stop(now + at + 0.55);
    }
    fx.chimes++;
  } catch (error) { /* audio unavailable */ }
}

const PIPS = { 1: [4], 2: [0, 8], 3: [0, 4, 8], 4: [0, 2, 6, 8], 5: [0, 2, 4, 6, 8], 6: [0, 2, 3, 5, 6, 8] };

function makeDie(side) {
  const die = document.createElement("span");
  die.className = `die ${side}`;
  for (let i = 0; i < 9; i++) {
    const pip = document.createElement("i");
    die.append(pip);
  }
  return die;
}

function setDieFace(die, value) {
  const on = PIPS[value] || [];
  [...die.children].forEach((pip, i) => pip.classList.toggle("on", on.includes(i)));
  die.setAttribute("aria-label", `${value}`);
}

function hideDice() {
  fx.diceTimers.forEach((timer) => clearTimeout(timer));
  fx.diceTimers = [];
  const overlay = $("dice-overlay");
  if (overlay) overlay.classList.add("hidden");
}

function maybeShowDice(game) {
  const attacks = (game.log || []).filter((event) =>
    event.kind === "attack" && event.battles && event.battles.length);
  const latest = attacks.length ? attacks[attacks.length - 1].version : 0;
  if (fx.diceGame !== game.id) {
    /* First load of this game: older rolls belong to the replay, not here. */
    fx.diceGame = game.id;
    fx.diceVersion = latest;
    return;
  }
  if (latest <= fx.diceVersion) return;
  fx.diceVersion = latest;
  if (!state.replayActive) showDice(attacks[attacks.length - 1]);
}

function showDice(event) {
  hideDice();
  const overlay = $("dice-overlay");
  const battles = event.battles;
  const battle = battles[battles.length - 1];
  const cap = state.mapMeta && state.mapMeta.rules && Number.isInteger(state.mapMeta.rules.dieCap)
    ? state.mapMeta.rules.dieCap : 6;
  overlay.replaceChildren();

  const title = document.createElement("div");
  title.className = "dice-title";
  title.textContent = `${playerName(event.player)} attacks ${mapName(event.to)} from ${mapName(event.from)}`;
  overlay.append(title);

  const rows = [];
  const makeRow = (label, side, values, bonus) => {
    const row = document.createElement("div");
    row.className = "dice-row";
    const name = document.createElement("span");
    name.className = "dice-label";
    name.textContent = label;
    row.append(name);
    const dice = values.map((value) => {
      const die = makeDie(side);
      setDieFace(die, value);
      row.append(die);
      return die;
    });
    if (bonus) {
      const chip = document.createElement("span");
      chip.className = "dice-chip";
      chip.textContent = `+${bonus}`;
      chip.title = "Leader, stronghold or card bonus on the highest die";
      dice[0].after(chip);
    }
    overlay.append(row);
    rows.push({ dice, values });
    return dice;
  };
  /* After a conquest the territory already belongs to the attacker, so the
   * defender's name is only known while they still hold it. */
  const owner = state.game && state.game.territories[event.to] ? state.game.territories[event.to].owner : -1;
  const attackerDice = makeRow(playerName(event.player), "attacker", battle.attacker || [], battle.attackerBonus);
  const defenderDice = makeRow(owner >= 0 && owner !== event.player ? playerName(owner) : "Defender",
    "defender", battle.defender || [], battle.defenderBonus);

  const pairs = Math.min(attackerDice.length, defenderDice.length);
  const outcome = [];
  for (let i = 0; i < pairs; i++) {
    const clamp = (value) => (cap > 0 ? Math.min(cap, value) : value);
    const a = clamp(battle.attacker[i] + (i === 0 ? battle.attackerBonus || 0 : 0));
    const d = clamp(battle.defender[i] + (i === 0 ? battle.defenderBonus || 0 : 0));
    outcome.push(a > d);
  }

  const result = document.createElement("div");
  result.className = "dice-result";
  const totalA = battles.reduce((sum, b) => sum + (b.attackerLosses || 0), 0);
  const totalD = battles.reduce((sum, b) => sum + (b.defenderLosses || 0), 0);
  let text = `Attacker −${battle.attackerLosses} · Defender −${battle.defenderLosses}`;
  if (battles.length > 1) {
    text = `Blitz, ${battles.length} rounds${event.truncated ? "+" : ""}: attacker −${totalA} · defender −${totalD}`;
  }
  if (owner === event.player) text +=` · ${mapName(event.to)} conquered`;
  result.textContent = text;
  overlay.append(result);

  const settle = () => {
    overlay.classList.remove("rolling");
    rows.forEach((row) => row.dice.forEach((die, i) => setDieFace(die, row.values[i])));
    for (let i = 0; i < pairs; i++) {
      attackerDice[i].classList.add(outcome[i] ? "won" : "lost");
      defenderDice[i].classList.add(outcome[i] ? "lost" : "won");
    }
    attackerDice.slice(pairs).forEach((die) => die.classList.add("spare"));
    defenderDice.slice(pairs).forEach((die) => die.classList.add("spare"));
    overlay.classList.add("settled");
    fx.diceTimers.push(setTimeout(hideDice, battles.length > 1 ? 4500 : 3200));
  };

  overlay.classList.remove("hidden", "settled");
  playEffect("dice_1.wav");
  if (fx.anim && !reducedMotion()) {
    overlay.classList.add("rolling");
    const spin = setInterval(() => {
      rows.forEach((row) => row.dice.forEach((die) => setDieFace(die, 1 + Math.floor(Math.random() * 6))));
    }, 75);
    fx.diceTimers.push(setTimeout(() => { clearInterval(spin); settle(); }, 600));
    fx.diceTimers.push(spin);
  } else {
    settle();
  }
}

function renderEndBanner() {
  const banner = $("end-banner");
  const game = state.game;
  if (!game || game.status !== "finished" || state.replayActive ||
      !state.replayChecked || fx.endDismissed) {
    banner.classList.add("hidden");
    return;
  }
  if (!banner.classList.contains("hidden")) return;
  banner.replaceChildren();
  const seated = game.you >= 0;
  const won = seated && game.winner === game.you;
  const heading = document.createElement("h2");
  heading.textContent = won ? "Victory!" : seated ? "Defeat" : "Game over";
  const line = document.createElement("p");
  line.textContent = game.winner >= 0
    ? `${playerName(game.winner)} wins after ${game.turn} turns.`
    : "The game ended without a winner.";
  banner.append(heading, line);

  const table = document.createElement("table");
  table.className = "end-stats";
  const head = table.createTHead().insertRow();
  for (const label of ["Player", "Lands", "Taken", "Killed", "Lost", "Missions"]) {
    const th = document.createElement("th");
    th.textContent = label;
    head.append(th);
  }
  const body = table.createTBody();
  const order = game.players.map((player, seat) => seat)
    .sort((a, b) => (b === game.winner) - (a === game.winner));
  for (const seat of order) {
    const player = game.players[seat];
    const stats = player.stats || {};
    const row = body.insertRow();
    if (seat === game.winner) row.className = "winner";
    const name = row.insertCell();
    name.append(dot(player.faction), document.createTextNode(` ${player.user}${seat === game.you ? " (you)" : ""}`));
    const lands = game.territories.filter((t) => t.owner === seat).length;
    for (const value of [lands, stats.territoriesTaken, stats.armiesKilled, stats.armiesLost, player.missions]) {
      row.insertCell().textContent = String(value ?? 0);
    }
  }
  banner.append(table);

  const buttons = document.createElement("div");
  buttons.className = "row";
  addButton(buttons, "Back to lobby", showLobby);
  addButton(buttons, "View the board", () => {
    fx.endDismissed = true;
    banner.classList.add("hidden");
  }, "secondary");
  banner.append(buttons);
  banner.classList.remove("hidden");
  if (fx.endSound) {
    fx.endSound = false;
    if (seated) playEffect(won ? "win.wav" : "lost.wav");
  }
}

$("dice-overlay").addEventListener("click", hideDice);
$("card-viewer").addEventListener("click", () => $("card-viewer").classList.add("hidden"));

$("dice-anim").checked = fx.anim;
$("dice-anim").addEventListener("change", (event) => {
  fx.anim = event.target.checked;
  try { localStorage.setItem("friskDiceAnim", fx.anim ? "on" : "off"); } catch (error) { /* ignore */ }
});
$("sfx-toggle").checked = fx.sound;
$("sfx-toggle").addEventListener("change", (event) => {
  fx.sound = event.target.checked;
  try { localStorage.setItem("friskSfx", fx.sound ? "on" : "off"); } catch (error) { /* ignore */ }
});

function musicAudio() {
  if (!music.audio) {
    music.audio = new Audio();
    music.audio.preload = "auto";
    music.audio.volume = music.volume;
    music.audio.addEventListener("ended", () => {
      if (music.mode === "bg" || music.mode === "combat") {
        setMusicMode(music.mode, true);
      }
    });
  }
  return music.audio;
}

function updateMusicButton() {
  const button = $("music-toggle");
  button.textContent = music.enabled ? "Music: on" : "Music: off";
  button.classList.toggle("secondary", true);
}

function setMusicMode(mode, force = false) {
  if (!music.armed || !music.enabled) {
    music.mode = mode;
    return;
  }
  if (!force && music.mode === mode && music.track) return;
  let name;
  if (mode === "combat") {
    name = COMBAT_TRACKS[music.combatIndex % COMBAT_TRACKS.length];
    music.combatIndex++;
  } else if (mode === "victory") {
    name = "victory.mp3";
  } else if (mode === "defeat") {
    name = "defeat.mp3";
  } else {
    mode = "bg";
    name = BG_TRACKS[music.bgIndex % BG_TRACKS.length];
    music.bgIndex++;
  }
  music.mode = mode;
  music.track = name;
  const audio = musicAudio();
  audio.loop = (mode === "victory" || mode === "defeat");
  audio.volume = music.volume;
  audio.src = `/music/${name}`;
  const playing = audio.play();
  if (playing) playing.catch(() => { /* autoplay is retried on the next gesture */ });
}

function updateMusic() {
  updateMusicButton();
  if (!music.armed || !music.enabled) return;
  if (state.viewMode !== "game" || !state.game || state.game.status === "open") {
    setMusicMode("bg");
    return;
  }
  const game = state.game;
  if (game.status === "finished") {
    if (game.you < 0) setMusicMode("bg");
    else setMusicMode(game.winner === game.you ? "victory" : "defeat");
    return;
  }
  if ((game.phase === "attack" || game.phase === "conquest_move") &&
      game.current === game.you) {
    music.combatUntil = Date.now() + 25000;
  }
  setMusicMode(Date.now() < music.combatUntil ? "combat" : "bg");
}

function armMusic() {
  if (!music.armed) {
    music.armed = true;
    updateMusic();
  } else if (music.enabled && music.audio && music.audio.paused && music.track) {
    const playing = music.audio.play();
    if (playing) playing.catch(() => { /* still not allowed */ });
  }
}

document.addEventListener("pointerdown", armMusic, true);
document.addEventListener("keydown", armMusic, true);
$("music-toggle").addEventListener("click", () => {
  music.enabled = !music.enabled;
  try { localStorage.setItem("friskMusic", music.enabled ? "on" : "off"); }
  catch (error) { /* storage unavailable */ }
  if (!music.enabled) {
    if (music.audio) music.audio.pause();
  } else {
    music.mode = null;
    music.track = null;
    updateMusic();
  }
  updateMusicButton();
});
$("music-volume").value = String(Math.round(music.volume * 100));
$("music-volume").addEventListener("input", () => {
  music.volume = Math.max(0, Math.min(1, Number($("music-volume").value) / 100));
  if (music.audio) music.audio.volume = music.volume;
  try { localStorage.setItem("friskMusicVolume", String(music.volume)); }
  catch (error) { /* storage unavailable */ }
});
updateMusicButton();

/* ---------------------------------------------------------------- */
/* Chat                                                              */
/* ---------------------------------------------------------------- */

const CHAT_TAUNTS = [
  "Horn", "Drum", "Sting", "Cackle",
  "Growl", "Chime", "Cry", "Doom"
];

function chatAutoplay() {
  try { return localStorage.getItem("friskChatAutoplay") === "on"; }
  catch (error) { return false; }
}

function setChatAutoplay(on) {
  try { localStorage.setItem("friskChatAutoplay", on ? "on" : "off"); }
  catch (error) { /* storage unavailable */ }
  for (const view of Object.values(chatViews)) {
    if (view.autoplay) view.autoplay.checked = on;
  }
}

function toast(message, bad = false) {
  const node = $("toast");
  node.textContent = message;
  node.classList.toggle("bad", !!bad);
  node.classList.remove("hidden");
  clearTimeout(state.toastTimer);
  state.toastTimer = setTimeout(() => node.classList.add("hidden"), 4500);
}

function say(id, text, ok = false) {
  const node = $(id);
  node.textContent = text || "";
  node.classList.toggle("ok", !!ok);
}

async function readJson(response) {
  try { return await response.json(); }
  catch (error) { return { ok: 0, err: "overflow" }; }
}

async function get(path) {
  /* AmmarServer marks every response max-age=3600, so API GETs need both a
   * no-store fetch and a unique URL to stay out of the browser cache. */
  const bust = `${path.includes("?") ? "&" : "?"}_=${Date.now()}-${++state.requestSeq}`;
  try {
    const response = await apiFetch(path + bust, {
      credentials: "same-origin", cache: "no-store"
    });
    return await readJson(response);
  } catch (error) {
    return { ok: 0, err: "network" };
  }
}

async function postRaw(path, fields) {
  const body = new FormData();
  for (const [key, value] of Object.entries(fields || {})) {
    if (value !== undefined && value !== null && value !== "") {
      if (value instanceof Blob) {
        body.append(key, value, value.name || "clip");
      } else {
        body.append(key, String(value));
      }
    }
  }
  try {
    const response = await apiFetch(path, {
      method: "POST", credentials: "same-origin", body, cache: "no-store"
    });
    return await readJson(response);
  } catch (error) {
    return { ok: 0, err: "network" };
  }
}

async function postForm(path, fields) {
  let response = await postRaw(path, { csrf: state.csrf, ...fields });
  if (!response.ok && response.err === "csrf" && await refreshMe(true)) {
    response = await postRaw(path, { csrf: state.csrf, ...fields });
  }
  return response;
}

async function post(path, fields) {
  let response = await postRaw(path, { ...fields, csrf: state.csrf });
  if (!response.ok && response.err === "csrf" && await refreshMe(true)) {
    response = await postRaw(path, { ...fields, csrf: state.csrf });
  }
  return response;
}

function errorMessage(result) {
  const base = errorText[result.err] || result.err || "Request failed.";
  return result.msg ? `${base} (${result.msg})` : base;
}

function noteOffline() {
  if (!state.offline) {
    state.offline = true;
    toast("Connection lost - retrying…", true);
  }
}

function noteOnline() {
  if (state.offline) {
    state.offline = false;
    toast("Reconnected.");
  }
}

async function apiFetch(path, options) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 12000);
  try {
    return await fetch(path, { ...options, signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

/* ---------------------------------------------------------------- */
/* Authentication                                                    */
/* ---------------------------------------------------------------- */

async function refreshMe(silent = false) {
  const result = await get("/api/me");
  if (result.err === "network") {
    noteOffline();
    return !!state.me;
  }
  if (result.user) {
    noteOnline();
    state.me = result;
    state.csrf = result.csrf || "";
    $("top-user").textContent = result.user;
    $("top-admin").classList.toggle("hidden", !result.admin);
    $("top-admin-link").classList.toggle("hidden", !result.admin);
    $("topbar").classList.remove("hidden");
    if (result.profile) {
      $("profile-colour").value = result.profile.colour || "#d9b23a";
      $("profile-glyph").value = result.profile.glyph || "";
    }
    if (!silent) applyHash();
    for (const view of Object.values(chatViews)) updateChatControls(view);
    return true;
  }
  state.me = null;
  state.csrf = "";
  $("topbar").classList.add("hidden");
  $("top-admin-link").classList.add("hidden");
  showAuth();
  return false;
}

function showAuth() {
  state.viewMode = "auth";
  document.body.classList.toggle("in-game", state.viewMode === "game");
  state.gameId = null;
  $("auth-view").classList.remove("hidden");
  $("lobby-view").classList.add("hidden");
  $("game-view").classList.add("hidden");
  $("admin-view").classList.add("hidden");
  $("topbar").classList.add("hidden");
  document.title = "FRISK";
  if (state.replayActive) cancelReplay(false);
  $("replay-bar").classList.add("hidden");
  deactivateChat(chatViews.lobby);
  deactivateChat(chatViews.game);
  resetChat(chatViews.lobby);
  resetChat(chatViews.game);
  updateMusic();
  if (!location.hash.startsWith("#/game/") && location.hash !== "#/login") {
    location.hash = "#/login";
  }
}

function showLobby() {
  if (!state.me) { showAuth(); return; }
  if (state.replayActive) cancelReplay(true);
  else if (state.viewMode === "game") markSeen();
  state.viewMode = "lobby";
  document.body.classList.toggle("in-game", state.viewMode === "game");
  $("auth-view").classList.add("hidden");
  $("lobby-view").classList.remove("hidden");
  $("game-view").classList.add("hidden");
  $("admin-view").classList.add("hidden");
  $("topbar").classList.remove("hidden");
  document.title = "FRISK";
  if (location.hash !== "#/lobby") location.hash = "#/lobby";
  loadMaps();
  refreshLobby(true);
  loadHistory();
  deactivateChat(chatViews.game);
  activateChat(chatView("lobby"), "lobby");
  updateMusic();
}

function showGame() {
  state.viewMode = "game";
  document.body.classList.toggle("in-game", state.viewMode === "game");
  applyTabs();
  $("auth-view").classList.add("hidden");
  $("lobby-view").classList.add("hidden");
  $("game-view").classList.remove("hidden");
  $("admin-view").classList.add("hidden");
  $("topbar").classList.remove("hidden");
  if (state.gameId) {
    const target = `#/game/${state.gameId}`;
    if (location.hash !== target) location.hash = target;
  }
}

function formatBytes(value) {
  const bytes = Number(value) || 0;
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function adminTable(headers) {
  const table = document.createElement("table");
  table.className = "admin-table";
  const head = document.createElement("thead");
  const row = document.createElement("tr");
  for (const label of headers) {
    const cell = document.createElement("th");
    cell.textContent = label;
    row.append(cell);
  }
  head.append(row);
  const body = document.createElement("tbody");
  table.append(head, body);
  return { table, body };
}

function adminCell(row, text) {
  const cell = document.createElement("td");
  cell.textContent = text;
  row.append(cell);
  return cell;
}

function renderAdminStorage(storage) {
  const box = $("admin-storage");
  box.replaceChildren();
  const grid = document.createElement("div");
  grid.className = "admin-storage";
  const add = (label, bytes, files) => {
    const name = document.createElement("span");
    name.className = "muted";
    name.textContent = label;
    const value = document.createElement("span");
    value.textContent = files === undefined
      ? formatBytes(bytes) : `${formatBytes(bytes)} · ${files} file${files === 1 ? "" : "s"}`;
    grid.append(name, value);
  };
  add("Game state", storage.games, storage.gamesFiles);
  add("Chat logs", storage.chat, storage.chatFiles);
  add("Voice clips", storage.clips, storage.clipFiles);
  add("Total", storage.total);
  box.append(grid);
}

function renderAdminUsers(users) {
  const box = $("admin-users");
  box.replaceChildren();
  const { table, body } = adminTable(["User", "Role", "Games", "Record", "Last seen"]);
  for (const user of users || []) {
    const row = document.createElement("tr");
    adminCell(row, user.username);
    adminCell(row, user.admin ? "admin" : "player");
    adminCell(row, String(user.gamesPlayed || 0));
    adminCell(row, `${user.wins || 0}W / ${user.losses || 0}L`);
    adminCell(row, user.lastSeen
      ? new Date(user.lastSeen * 1000).toLocaleString() : "—");
    body.append(row);
  }
  if (!body.children.length) {
    const row = document.createElement("tr");
    const cell = adminCell(row, "No profiles yet.");
    cell.colSpan = 5;
    body.append(row);
  }
  box.append(table);
}

function renderAdminInvites(invites) {
  const box = $("admin-invites");
  box.replaceChildren();
  const { table, body } = adminTable(["Code", "Creator", "Status", "Actions"]);
  for (const invite of invites || []) {
    const row = document.createElement("tr");
    adminCell(row, invite.code);
    adminCell(row, invite.creator);
    let status = "open";
    if (invite.revoked) status = "revoked";
    else if (invite.used) status = `used by ${invite.usedBy || "someone"}`;
    adminCell(row, status);
    const actions = document.createElement("td");
    actions.className = "admin-actions";
    if (!invite.used && !invite.revoked) {
      const revoke = document.createElement("button");
      revoke.type = "button";
      revoke.className = "secondary";
      revoke.textContent = "Revoke";
      revoke.addEventListener("click", () => {
        if (!confirm(`Revoke invite ${invite.code}?`)) return;
        adminAction("/api/invite/revoke", { code: invite.code }, "Invite revoked.");
      });
      actions.append(revoke);
    }
    row.append(actions);
    body.append(row);
  }
  if (!body.children.length) {
    const row = document.createElement("tr");
    const cell = adminCell(row, "No invite codes.");
    cell.colSpan = 4;
    body.append(row);
  }
  box.append(table);
}

function renderAdminGames(games) {
  const box = $("admin-games");
  box.replaceChildren();
  const { table, body } = adminTable(["Game", "Status", "Players", "Seats", "Actions"]);
  for (const game of games || []) {
    const row = document.createElement("tr");
    adminCell(row, `${game.name} (#${game.id})`);
    adminCell(row, `${game.status} · ${game.phase}`);
    adminCell(row, `${game.players}/${game.maxPlayers}`);
    const seats = document.createElement("td");
    for (const seat of game.seats || []) {
      const line = document.createElement("div");
      line.className = "admin-actions";
      const label = document.createElement("span");
      label.textContent = `${seat.user}${seat.alive ? "" : " (out)"} `;
      line.append(label);
      if (game.status === "running" &&
          ["reinforce", "attack", "fortify"].includes(game.phase) && seat.alive) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "danger";
        button.textContent = "Neutralise";
        button.addEventListener("click", () => {
          if (!confirm(`Neutralise ${seat.user} in "${game.name}"?`)) return;
          adminAction("/api/admin/neutralise",
            { g: game.id, seat: seat.seat }, `${seat.user} neutralised.`);
        });
        line.append(button);
      }
      seats.append(line);
    }
    row.append(seats);
    const actions = document.createElement("td");
    actions.className = "admin-actions";
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "danger";
    remove.textContent = "Delete";
    remove.addEventListener("click", () => {
      if (!confirm(`Delete game "${game.name}"? This cannot be undone.`)) return;
      adminAction("/api/game/delete", { g: game.id }, "Game deleted.");
    });
    actions.append(remove);
    row.append(actions);
    body.append(row);
  }
  if (!body.children.length) {
    const row = document.createElement("tr");
    const cell = adminCell(row, "No games.");
    cell.colSpan = 5;
    body.append(row);
  }
  box.append(table);
}

function renderAdmin(data) {
  renderAdminStorage(data.storage || {});
  renderAdminUsers(data.users || []);
  renderAdminInvites(data.invites || []);
  renderAdminGames(data.games || []);
}

async function loadAdmin() {
  if (!state.me || !state.me.admin) return;
  const result = await get("/api/admin/overview");
  if (!result || result.err) {
    if (result && result.err === "login") {
      await refreshMe(true);
      return;
    }
    say("admin-msg", errorMessage(result || {}), false);
    return;
  }
  say("admin-msg", "");
  renderAdmin(result);
}

async function adminAction(path, fields, message) {
  say("admin-msg", "");
  const result = await post(path, fields);
  if (!result.ok) {
    say("admin-msg", errorMessage(result), false);
    return;
  }
  say("admin-msg", message, true);
  await loadAdmin();
  refreshLobby(true);
}

function showAdmin() {
  if (!state.me) { showAuth(); return; }
  if (!state.me.admin) {
    toast(errorMessage({ err: "admin" }), true);
    showLobby();
    return;
  }
  if (state.replayActive) cancelReplay(true);
  else if (state.viewMode === "game") markSeen();
  state.viewMode = "admin";
  document.body.classList.toggle("in-game", state.viewMode === "game");
  $("auth-view").classList.add("hidden");
  $("lobby-view").classList.add("hidden");
  $("game-view").classList.add("hidden");
  $("admin-view").classList.remove("hidden");
  $("topbar").classList.remove("hidden");
  document.title = "Admin — FRISK";
  if (location.hash !== "#/admin") location.hash = "#/admin";
  deactivateChat(chatViews.lobby);
  deactivateChat(chatViews.game);
  resetChat(chatViews.lobby);
  resetChat(chatViews.game);
  loadAdmin();
  updateMusic();
}

$("top-admin-link").addEventListener("click", () => {
  if (!state.me || !state.me.admin) return;
  if (state.viewMode === "admin") loadAdmin();
  else location.hash = "#/admin";
});
$("admin-refresh").addEventListener("click", loadAdmin);

function applyHash() {
  const match = /^#\/game\/([0-9]+)$/.exec(location.hash);
  if (!state.me) { showAuth(); return; }
  if (location.hash === "#/admin") {
    if (!state.me.admin) { showLobby(); return; }
    if (state.viewMode === "admin") return;
    showAdmin();
    return;
  }
  if (match) {
    const id = Number(match[1]);
    if (state.viewMode === "game" && state.gameId === id) return;
    openGame(id);
    return;
  }
  if (state.viewMode !== "lobby") showLobby();
  else refreshLobby(false);
}

window.addEventListener("hashchange", applyHash);

$("login-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  say("login-msg", "");
  const result = await postRaw("/api/login", {
    name: $("login-name").value,
    password: $("login-pass").value
  });
  if (!result.ok) { say("login-msg", errorMessage(result)); return; }
  $("login-pass").value = "";
  await refreshMe();
});

$("register-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  say("register-msg", "");
  const result = await postRaw("/api/register", {
    name: $("reg-name").value,
    password: $("reg-pass").value,
    invite: $("reg-invite").value
  });
  if (!result.ok) { say("register-msg", errorMessage(result)); return; }
  $("reg-pass").value = "";
  $("reg-invite").value = "";
  await refreshMe();
});

$("top-logout").addEventListener("click", async () => {
  await post("/api/logout", {});
  state.me = null;
  state.csrf = "";
  location.hash = "#/login";
  showAuth();
});

$("top-lobby").addEventListener("click", showLobby);
$("back-to-lobby").addEventListener("click", showLobby);
$("resign-game").addEventListener("click", async () => {
  if (!state.game) return;
  if (!confirm("Resign? Your lands become neutral.")) return;
  await act("resign", {});
});

/* ---------------------------------------------------------------- */
/* Lobby                                                             */
/* ---------------------------------------------------------------- */

async function loadMaps() {
  const select = $("create-map");
  if (select.options.length > 0) return;
  const result = await get("/api/map");
  if (!result.ok) return;
  select.replaceChildren();
  for (const pack of result.packs || []) {
    const option = document.createElement("option");
    option.value = pack;
    option.textContent = pack;
    select.append(option);
  }
  if (result.default) select.value = result.default;
}

async function refreshLobby(force = false) {
  if (state.viewMode !== "lobby" || !state.me) return;
  const path = "/api/lobby" + (force || !state.lobbySince
    ? "" : `?since=${state.lobbySince}`);
  const result = await get(path);
  if (!result.ok) {
    if (result.err === "network") { noteOffline(); return; }
    if (result.err === "login") { await refreshMe(); }
    return;
  }
  noteOnline();
  if (result.same) { state.lobbyAt = Date.now(); return; }
  state.lobby = result;
  state.lobbySince = result.version || 0;
  state.lobbyAt = Date.now();
  renderLobby();
}

const packMeta = {};
const packLoading = new Set();

function loadPackMeta(pack) {
  if (!pack || packMeta[pack] || packLoading.has(pack)) return;
  packLoading.add(pack);
  fetch(`/maps/${encodeURIComponent(pack)}/map.json`)
    .then((response) => response.json())
    .then((meta) => {
      packMeta[pack] = meta;
      if (state.viewMode === "lobby") renderLobby();
    })
    .catch(() => { /* dots stay grey */ })
    .finally(() => packLoading.delete(pack));
}

function factionColour(faction, pack) {
  const meta = (pack && packMeta[pack]) || state.mapMeta;
  if (meta && meta.factions && meta.factions[faction]) {
    return meta.factions[faction].color || "#999";
  }
  return "#999";
}

function dot(faction, pack) {
  const span = document.createElement("span");
  span.className = "player-dot";
  span.style.background = factionColour(faction, pack);
  return span;
}

function gameItem(game, kind) {
  loadPackMeta(game.map);
  const item = document.createElement("div");
  item.className = "game-item";
  const title = document.createElement("div");
  title.className = "title";
  const name = document.createElement("span");
  name.textContent = game.name;
  title.append(name);
  if (game.yourTurn) {
    const badge = document.createElement("span");
    badge.className = "badge turn";
    badge.textContent = "YOUR TURN";
    title.append(badge);
  }
  const status = document.createElement("span");
  status.className = "badge";
  status.textContent = game.status;
  title.append(status);
  const meta = document.createElement("div");
  meta.className = "meta";
  const parts = [
    `${game.playerCount}/${game.maxPlayers} players`,
    game.status === "running" ? `turn ${game.turn}` : game.status === "finished" ? `winner ${game.winner >= 0 ? game.players[game.winner]?.user || "?" : "none"}` : `host ${game.host || "?"}`
  ];
  meta.textContent = parts.join(" · ");
  const who = document.createElement("div");
  who.className = "meta";
  (game.players || []).forEach((player) => {
    const span = document.createElement("span");
    span.style.marginRight = "8px";
    span.append(dot(player.faction, game.map), document.createTextNode(" " + player.user));
    who.append(span);
  });
  item.append(title, meta, who);

  const buttons = document.createElement("div");
  buttons.className = "buttons";
  const add = (label, cls, handler) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = cls || "tiny";
    button.textContent = label;
    button.addEventListener("click", handler);
    buttons.append(button);
  };
  add("Enter", "tiny", () => openGame(game.id));
  if (kind === "open" && game.yourSeat < 0) {
    add("Join", "tiny", () => joinGame(game));
  }
  if (kind === "your" && game.status === "open" && game.host === state.me.user) {
    add("Start", "tiny", () => startGame(game.id));
  }
  if (kind === "your" && (game.status === "open" || game.status === "finished") &&
      game.host === state.me.user) {
    add("Delete", "secondary tiny", () => deleteGame(game.id));
  }
  if (kind === "your" && game.status === "open") {
    add("Leave", "secondary tiny", () => leaveGame(game.id));
  }
  item.append(buttons);
  return item;
}

function renderList(id, games, kind) {
  const list = $(id);
  list.replaceChildren();
  if (!games || games.length === 0) {
    const empty = document.createElement("p");
    empty.className = "small muted";
    empty.textContent = kind === "open" ? "No open tables to join." : kind === "running" ? "No other games to watch." : "No games yet - create one below.";
    list.append(empty);
    return;
  }
  for (const game of games) list.append(gameItem(game, kind));
}

function renderLobby() {
  const data = state.lobby;
  if (!data) return;
  const sorted = (games, byTurn) => (games || []).slice().sort((a, b) => {
    if (byTurn && a.yourTurn !== b.yourTurn) return a.yourTurn ? -1 : 1;
    return (b.updated || 0) - (a.updated || 0);
  });
  renderList("your-games", sorted(data.yourGames, true), "your");
  /* Your own games are already listed first; the other lists are for joining
   * and spectating. */
  const others = (games) => (games || []).filter((game) => game.yourSeat < 0);
  renderList("open-games", sorted(others(data.openGames), false), "open");
  renderList("running-games", sorted(others(data.runningGames), false), "running");
  const turns = new Set((data.yourGames || []).filter((game) => game.yourTurn).map((game) => game.id));
  if (fx.lobbyTurns && [...turns].some((id) => !fx.lobbyTurns.has(id))) playTurnChime();
  fx.lobbyTurns = turns;
  maybeNotifyLobby();
}

$("create-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  say("create-msg", "");
  const result = await post("/api/game/create", {
    name: $("create-name").value,
    map: $("create-map").value,
    seats: $("create-seats").value,
    mode: $("create-mode").value,
    batch: $("create-batch").value,
    password: $("create-password").value
  });
  if (!result.ok) { say("create-msg", errorMessage(result)); return; }
  $("create-password").value = "";
  await refreshLobby(true);
  openGame(result.id);
});

async function joinGame(game) {
  let password = "";
  if (game.hasPassword) {
    const entered = prompt(`Password for "${game.name}":`);
    if (entered === null) return;
    password = entered;
  }
  const result = await post("/api/game/join", { g: game.id, password });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  await refreshLobby(true);
  openGame(game.id);
}

async function startGame(id) {
  const result = await post("/api/game/start", { g: id });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  await refreshLobby(true);
  openGame(id);
}

async function leaveGame(id) {
  const result = await post("/api/game/leave", { g: id });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  refreshLobby(true);
}

async function deleteGame(id) {
  if (!confirm("Delete this game? The file is removed.")) return;
  const result = await post("/api/game/delete", { g: id });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  refreshLobby(true);
}

$("new-invite").addEventListener("click", async () => {
  say("invite-msg", "");
  const result = await post("/api/invite/new", {});
  if (!result.ok) { say("invite-msg", errorMessage(result)); return; }
  say("invite-msg", `Invite ${result.code} created.`, true);
  loadInvites();
});

async function loadInvites() {
  const result = await get("/api/invite/list");
  const list = $("invites");
  list.replaceChildren();
  if (!result.ok) { say("invite-msg", errorMessage(result)); return; }
  for (const invite of result.invites || []) {
    const item = document.createElement("li");
    item.style.padding = "6px 0";
    item.style.borderBottom = "1px solid var(--line)";
    const code = document.createElement("code");
    code.textContent = invite.code;
    const status = document.createElement("span");
    status.className = "muted";
    status.textContent = invite.revoked ? " revoked" : invite.used ? ` used by ${invite.usedBy}` : " unused";
    item.append(code, status);
    if (!invite.used && !invite.revoked) {
      const revoke = document.createElement("button");
      revoke.type = "button";
      revoke.className = "secondary tiny";
      revoke.style.marginLeft = "8px";
      revoke.textContent = "Revoke";
      revoke.addEventListener("click", async () => {
        const result = await post("/api/invite/revoke", { code: invite.code });
        if (!result.ok) { toast(errorMessage(result), true); return; }
        loadInvites();
      });
      item.append(revoke);
    }
    list.append(item);
  }
}

$("refresh-invites").addEventListener("click", loadInvites);

async function loadHistory() {
  const list = $("history");
  const result = await get("/api/profile");
  if (!result.ok) return;
  list.replaceChildren();
  const entries = (result.history || []).slice().reverse();
  if (entries.length === 0) {
    const empty = document.createElement("li");
    empty.className = "muted";
    empty.textContent = "No finished games yet.";
    list.append(empty);
    return;
  }
  for (const entry of entries) {
    const me = (entry.players || []).find((player) => player.user === result.profile.username);
    const item = document.createElement("li");
    const outcome = document.createElement("strong");
    outcome.className = me && me.won ? "won" : "lost";
    outcome.textContent = me && me.won ? "Won" : "Lost";
    const when = entry.finished ? new Date(entry.finished * 1000).toLocaleDateString() : "";
    const others = (entry.players || []).map((player) => player.user).join(", ");
    item.append(outcome, document.createTextNode(
      ` · ${entry.name} · ${entry.turns} turns · ${when} · ${others}` +
      (me ? ` · took ${me.taken}, killed ${me.killed}, lost ${me.lost}` : "")));
    list.append(item);
  }
}

$("save-profile").addEventListener("click", async () => {
  say("profile-msg", "");
  const result = await post("/api/profile/set", {
    colour: $("profile-colour").value,
    glyph: $("profile-glyph").value
  });
  if (!result.ok) { say("profile-msg", errorMessage(result)); return; }
  say("profile-msg", "Saved.", true);
  if (state.me && result.profile) state.me.profile = result.profile;
});

/* ---------------------------------------------------------------- */
/* Game state and map                                                */
/* ---------------------------------------------------------------- */

async function openGame(id) {
  state.gameId = id;
  state.game = null;
  state.mapMeta = null;
  state.mapPack = null;
  state.svg = null;
  state.badges = null;
  state.pieceSymbols = null;
  state.hover = null;
  state.selectedFrom = null;
  state.selectedTo = null;
  state.pendingPlace = null;
  state.pendingLeader = null;
  state.cardPlay = null;
  state.bringLeader = false;
  state.replayChecked = false;
  state.replayActive = false;
  state.replayEvents = [];
  state.replayIndex = 0;
  state.replayFrom = null;
  state.replayTo = null;
  state.notifiedTurn = -1;
  fx.diceGame = null;
  clearTimeout(fx.infoTimer);
  $("territory-info").classList.add("hidden");
  tabs.logSeen = null;
  tabs.chatUnread = 0;
  tabs.open = false;
  fx.endDismissed = false;
  fx.endSound = false;
  hideDice();
  $("end-banner").classList.add("hidden");
  clearTimeout(state.replayTimer);
  $("replay-bar").classList.add("hidden");
  $("map-stage").replaceChildren();
  $("game-actions").replaceChildren();
  $("game-hand").replaceChildren();
  $("game-log").replaceChildren();
  $("game-name").textContent = "Loading…";
  showGame();
  deactivateChat(chatViews.lobby);
  activateChat(chatView("game"), `g${id}`);
  await loadGame(true);
}

async function loadGame(force) {
  if (!state.gameId || state.loadingGame) return;
  state.loadingGame = true;
  state.gameAt = Date.now();
  try {
    const since = (!force && state.game) ? state.game.version : 0;
    const result = await get(`/api/game/state?g=${state.gameId}${since ? `&since=${since}` : ""}`);
    if (!result || result.err) {
      if (result && result.err === "login") { await refreshMe(true); return; }
      if (result && result.err === "network") { noteOffline(); return; }
      if (result && result.err === "notfound") { showLobby(); return; }
      toast(errorMessage(result || {}), true);
      return;
    }
    noteOnline();
    if (result.same) return;
    const previousStatus = state.game ? state.game.status : null;
    const wasMine = !!(state.game && state.game.id === result.id && isMyTurn(state.game));
    const firstLoad = !(state.game && state.game.id === result.id);
    state.game = result;
    if (!firstLoad && !wasMine && isMyTurn(result)) playTurnChime();
    if (!(await ensureMap(result.map))) return;
    renderGame();
    if (!state.replayChecked) {
      state.replayChecked = true;
      refitIfFitted();
      const you = result.you >= 0 ? result.players[result.you] : null;
      fx.endSound = result.status === "finished" && !!you && you.lastSeen < result.version;
      await maybeStartReplay(result);
    } else if (previousStatus === "running" && result.status === "finished") {
      fx.endSound = true;
    }
    maybeShowDice(result);
    renderEndBanner();
  } finally {
    state.loadingGame = false;
  }
}

function installFactionPatterns(svg, meta) {
  const defs = document.createElementNS(NS, "defs");
  const factions = (meta && meta.factions) || [];
  for (let i = 0; i < 4; i++) {
    const pattern = document.createElementNS(NS, "pattern");
    pattern.setAttribute("id", `friskPat${i}`);
    pattern.setAttribute("patternUnits", "userSpaceOnUse");
    pattern.setAttribute("width", "14");
    pattern.setAttribute("height", "14");
    const background = document.createElementNS(NS, "rect");
    background.setAttribute("width", "14");
    background.setAttribute("height", "14");
    background.setAttribute("fill", (factions[i] && factions[i].color) || "#888888");
    pattern.append(background);
    const shapes = [];
    if (i === 0) {
      shapes.push(["path", { d: "M -2 16 L 16 -2", stroke: "#fff7e8", "stroke-width": "3" }]);
    } else if (i === 1) {
      shapes.push(["path", { d: "M 0 4 H 14 M 0 11 H 14", stroke: "#fff7e8", "stroke-width": "2" }]);
    } else if (i === 2) {
      shapes.push(["circle", { cx: "4", cy: "4", r: "2.2", fill: "#fff7e8" }]);
      shapes.push(["circle", { cx: "11", cy: "11", r: "2.2", fill: "#fff7e8" }]);
    } else {
      shapes.push(["path", { d: "M -2 0 L 14 16 M 16 0 L 0 16", stroke: "#fff7e8", "stroke-width": "2" }]);
    }
    for (const [tag, attributes] of shapes) {
      const shape = document.createElementNS(NS, tag);
      for (const [key, value] of Object.entries(attributes)) shape.setAttribute(key, value);
      shape.setAttribute("opacity", "0.72");
      pattern.append(shape);
    }
    defs.append(pattern);
  }
  const neutral = document.createElementNS(NS, "pattern");
  neutral.setAttribute("id", "friskPatN");
  neutral.setAttribute("patternUnits", "userSpaceOnUse");
  neutral.setAttribute("width", "12");
  neutral.setAttribute("height", "12");
  const neutralBg = document.createElementNS(NS, "rect");
  neutralBg.setAttribute("width", "12");
  neutralBg.setAttribute("height", "12");
  neutralBg.setAttribute("fill", "#777777");
  const neutralDot = document.createElementNS(NS, "circle");
  neutralDot.setAttribute("cx", "6");
  neutralDot.setAttribute("cy", "6");
  neutralDot.setAttribute("r", "1.6");
  neutralDot.setAttribute("fill", "#e8e2d2");
  neutral.append(neutralBg, neutralDot);
  defs.append(neutral);
  svg.insertBefore(defs, svg.firstChild);
}

async function ensureMap(pack) {
  if (state.mapPack === pack && state.svg) return true;
  try {
    const meta = await fetch(`/maps/${encodeURIComponent(pack)}/map.json`).then((r) => r.json());
    let text = await fetch(`/maps/${encodeURIComponent(pack)}/board.svg`).then((r) => r.text());
    text = text.split('href="board.jpg"').join(`href="/maps/${encodeURIComponent(pack)}/board.jpg"`);
    const doc = new DOMParser().parseFromString(text, "image/svg+xml");
    if (doc.querySelector("parsererror")) throw new Error("invalid SVG");
    const svg = doc.documentElement;
    const width = Number(meta.image && meta.image.width) || MAP_W;
    const height = Number(meta.image && meta.image.height) || MAP_H;
    svg.setAttribute("width", String(width));
    svg.setAttribute("height", String(height));
    if (!svg.getAttribute("viewBox")) {
      svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    }
    svg.style.width = `${width}px`;
    svg.style.height = `${height}px`;
    $("map-stage").style.width = `${width}px`;
    $("map-stage").style.height = `${height}px`;
    installFactionPatterns(svg, meta);
    const badges = document.createElementNS(NS, "g");
    badges.setAttribute("id", "badges");
    svg.appendChild(badges);
    svg.querySelectorAll("path.territory").forEach((path) => {
      path.setAttribute("data-t", path.id.replace("t", ""));
    });
    svg.insertBefore(buildHitAreas(meta, width, height), badges);
    state.pieceSymbols = await loadPieces(svg, meta, pack);
    svg.addEventListener("pointerdown", (event) => { fx.lastPointer = event.pointerType || "mouse"; });
    svg.addEventListener("click", (event) => {
      const hit = event.target.closest && event.target.closest("[data-t]");
      if (!hit || state.suppressClick) return;
      event.stopPropagation();
      const territory = Number(hit.getAttribute("data-t"));
      if (fx.lastPointer !== "mouse") showInfo(territory, true);
      const now = Date.now();
      const tap = fx.lastTap;
      fx.lastTap = { territory, at: now };
      if (tap && tap.territory === territory && now - tap.at < 350) {
        fx.lastTap = null;
        zoomToTerritory(territory);
        return;
      }
      territoryClick(territory);
    });
    svg.addEventListener("pointerover", (event) => {
      if (event.pointerType !== "mouse") return;
      const hit = event.target.closest && event.target.closest("[data-t]");
      setHover(hit ? Number(hit.getAttribute("data-t")) : null);
    });
    svg.addEventListener("pointerleave", () => setHover(null));
    $("map-stage").replaceChildren(svg);
    state.mapMeta = meta;
    state.mapPack = pack;
    packMeta[pack] = meta;
    state.mapWidth = width;
    state.mapHeight = height;
    state.svg = svg;
    state.badges = badges;
    fitMap();
    return true;
  } catch (error) {
    toast(`Could not load map "${pack}": ${error.message}`, true);
    return false;
  }
}

/* The pack's territory shapes are small anchor hexes (about 40 map px, so
 * 12-17 px on a fitted board).  Each territory also gets an invisible hit
 * area: its nearest-label (Voronoi) cell, capped to a radius, so a tap
 * anywhere near a territory's badge selects it. */
function clipHalfPlane(polygon, a, b, c) {
  const out = [];
  for (let k = 0; k < polygon.length; k++) {
    const p = polygon[k];
    const q = polygon[(k + 1) % polygon.length];
    const fp = a * p[0] + b * p[1] - c;
    const fq = a * q[0] + b * q[1] - c;
    if (fp <= 0) out.push(p);
    if ((fp < 0 && fq > 0) || (fp > 0 && fq < 0)) {
      const t = fp / (fp - fq);
      out.push([p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t]);
    }
  }
  return out;
}

function hitCell(points, index, width, height, radius) {
  const [px, py] = points[index];
  let cell = [];
  for (let k = 0; k < 24; k++) {
    const angle = (k / 24) * Math.PI * 2;
    cell.push([px + Math.cos(angle) * radius, py + Math.sin(angle) * radius]);
  }
  cell = clipHalfPlane(cell, -1, 0, 0);
  cell = clipHalfPlane(cell, 1, 0, width);
  cell = clipHalfPlane(cell, 0, -1, 0);
  cell = clipHalfPlane(cell, 0, 1, height);
  for (let j = 0; j < points.length && cell.length >= 3; j++) {
    if (j === index || !points[j]) continue;
    const [qx, qy] = points[j];
    const nx = qx - px;
    const ny = qy - py;
    cell = clipHalfPlane(cell, nx, ny, nx * (px + qx) / 2 + ny * (py + qy) / 2);
  }
  return cell;
}

function buildHitAreas(meta, width, height) {
  const group = document.createElementNS(NS, "g");
  group.setAttribute("id", "hit-areas");
  const territories = (meta && meta.territories) || [];
  const points = territories.map((t) => (Array.isArray(t.label) && t.label.length === 2
    ? [Number(t.label[0]), Number(t.label[1])] : null));
  const count = points.filter(Boolean).length;
  if (!count) return group;
  const radius = 0.7 * Math.sqrt((width * height) / count);
  points.forEach((point, index) => {
    if (!point) return;
    const cell = hitCell(points, index, width, height, radius);
    if (cell.length < 3) return;
    const path = document.createElementNS(NS, "path");
    path.setAttribute("class", "hit");
    path.setAttribute("data-t", String(index));
    path.setAttribute("d", "M" + cell.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join("L") + "Z");
    group.append(path);
  });
  return group;
}

function territoryInfo(index) {
  const game = state.game;
  const meta = state.mapMeta;
  const def = meta && meta.territories && meta.territories[index];
  if (!game || !def || !game.territories[index]) return null;
  const territory = game.territories[index];
  const box = document.createElement("div");
  const name = document.createElement("strong");
  name.textContent = def.name || `#${index}`;
  box.append(name);
  const region = (meta.regions || []).find((entry) => entry.id === def.region);
  if (region) {
    const span = document.createElement("span");
    span.className = "muted";
    span.textContent = ` · ${region.name} (+${region.bonus})`;
    box.append(span);
  }
  const line = document.createElement("div");
  if (territory.neutral) {
    line.append(document.createTextNode(`Neutral · ${territory.armies} armies`));
  } else if (territory.owner >= 0) {
    line.append(dot(playerFaction(territory.owner)),
      document.createTextNode(` ${playerName(territory.owner)} · ${territory.armies} ${territory.armies === 1 ? "army" : "armies"}`));
  } else {
    line.append(document.createTextNode("Unclaimed"));
  }
  box.append(line);
  const marks = [];
  if (territory.leader) marks.push(`★ ${playerName(territory.leader - 1)}'s leader`);
  if (def.stronghold) marks.push("⌂ stronghold");
  if (def.site) marks.push("✦ site of power");
  if (marks.length) {
    const extra = document.createElement("div");
    extra.className = "muted";
    extra.textContent = marks.join(" · ");
    box.append(extra);
  }
  return box;
}

/* Hover (mouse) or tap (touch) shows what a territory is; touch hides it
 * again after a few seconds. */
function showInfo(index, autoHide) {
  const chip = $("territory-info");
  clearTimeout(fx.infoTimer);
  const content = index === null ? null : territoryInfo(index);
  if (!content) {
    chip.classList.add("hidden");
    return;
  }
  chip.replaceChildren(content);
  chip.classList.remove("hidden");
  if (autoHide) fx.infoTimer = setTimeout(() => chip.classList.add("hidden"), 3500);
}

function setHover(index) {
  if (state.hover === index) return;
  const old = state.svg && state.hover !== null ? state.svg.querySelector(`#t${state.hover}`) : null;
  if (old) old.classList.remove("hover");
  state.hover = index;
  const next = state.svg && index !== null ? state.svg.querySelector(`#t${index}`) : null;
  if (next) next.classList.add("hover");
  showInfo(index, false);
}

function fitMap() {
  const container = $("map-container");
  const width = container.clientWidth || 800;
  const height = container.clientHeight || 600;
  const scale = Math.min(width / state.mapWidth, height / state.mapHeight) * 0.98;
  state.view = {
    scale,
    x: (width - state.mapWidth * scale) / 2,
    y: (height - state.mapHeight * scale) / 2
  };
  state.viewFitted = true;
  applyView();
}

/* Refit after the map container changed size (replay bar, first render of
 * the controls), unless the player has already panned or zoomed. */
function refitIfFitted() {
  if (state.viewFitted && state.svg) requestAnimationFrame(fitMap);
}

function zoomToTerritory(index) {
  const meta = state.mapMeta;
  const label = meta && meta.territories && meta.territories[index] && meta.territories[index].label;
  if (!label) return;
  const container = $("map-container");
  const scale = Math.min(5, Math.max(state.view.scale * 2, 1.4));
  state.view = {
    scale,
    x: (container.clientWidth || 800) / 2 - label[0] * scale,
    y: (container.clientHeight || 600) / 2 - label[1] * scale
  };
  state.viewFitted = false;
  applyView();
}

function applyView() {
  $("map-stage").style.transform =
    `translate(${state.view.x}px, ${state.view.y}px) scale(${state.view.scale})`;
}

function zoomAt(clientX, clientY, factor) {
  const container = $("map-container");
  const rect = container.getBoundingClientRect();
  const cx = clientX - rect.left;
  const cy = clientY - rect.top;
  const view = state.view;
  const next = Math.max(0.15, Math.min(5, view.scale * factor));
  const ratio = next / view.scale;
  view.x = cx - (cx - view.x) * ratio;
  view.y = cy - (cy - view.y) * ratio;
  view.scale = next;
  state.viewFitted = false;
  applyView();
}

$("map-fit").addEventListener("click", fitMap);
$("map-zoom-in").addEventListener("click", () => {
  const rect = $("map-container").getBoundingClientRect();
  zoomAt(rect.left + rect.width / 2, rect.top + rect.height / 2, 1.25);
});
$("map-zoom-out").addEventListener("click", () => {
  const rect = $("map-container").getBoundingClientRect();
  zoomAt(rect.left + rect.width / 2, rect.top + rect.height / 2, 0.8);
});

const mapContainer = $("map-container");
mapContainer.addEventListener("wheel", (event) => {
  event.preventDefault();
  zoomAt(event.clientX, event.clientY, event.deltaY < 0 ? 1.12 : 0.89);
}, { passive: false });

function pinchInfo() {
  const points = [...state.pointers.values()];
  const dx = points[0].x - points[1].x;
  const dy = points[0].y - points[1].y;
  return {
    distance: Math.max(1, Math.hypot(dx, dy)),
    scale: state.view.scale,
    midX: (points[0].x + points[1].x) / 2,
    midY: (points[0].y + points[1].y) / 2
  };
}

function capturePointer(id) {
  try { mapContainer.setPointerCapture(id); } catch (error) { /* pointer already gone */ }
}

/* Pointer capture starts only once a press becomes a drag or pinch.  Capturing
 * on pointerdown retargets the click that ends a plain press to the container,
 * so territories never received mouse clicks. */
mapContainer.addEventListener("pointerdown", (event) => {
  if (tabs.open && dockLayout()) {
    tabs.open = false;
    applyTabs();
  }
  hideDice();
  state.pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
  mapContainer.classList.add("dragging");
  if (state.pointers.size === 1) {
    state.drag = {
      x: event.clientX, y: event.clientY,
      startX: event.clientX, startY: event.clientY, moved: false
    };
  } else {
    state.drag = null;
    state.pinch = pinchInfo();
    state.pointers.forEach((point, id) => capturePointer(id));
  }
});

mapContainer.addEventListener("pointermove", (event) => {
  if (!state.pointers.has(event.pointerId)) return;
  if (event.pointerType === "mouse" && event.buttons === 0) {
    /* Released outside the window before the drag captured the pointer. */
    endPointer(event);
    return;
  }
  state.pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
  if (state.pointers.size === 2 && state.pinch) {
    const info = pinchInfo();
    const view = state.view;
    const next = Math.max(0.15, Math.min(5, state.pinch.scale * (info.distance / state.pinch.distance)));
    const ratio = next / view.scale;
    const rect = mapContainer.getBoundingClientRect();
    const cx = state.pinch.midX - rect.left;
    const cy = state.pinch.midY - rect.top;
    view.x = cx - (cx - view.x) * ratio;
    view.y = cy - (cy - view.y) * ratio;
    view.scale = next;
    view.x += info.midX - state.pinch.midX;
    view.y += info.midY - state.pinch.midY;
    state.pinch = info;
    state.viewFitted = false;
    applyView();
    state.suppressClick = true;
  } else if (state.drag) {
    const dx = event.clientX - state.drag.x;
    const dy = event.clientY - state.drag.y;
    state.view.x += dx;
    state.view.y += dy;
    state.drag.x = event.clientX;
    state.drag.y = event.clientY;
    if (!state.drag.moved &&
        Math.hypot(event.clientX - state.drag.startX, event.clientY - state.drag.startY) > 6) {
      state.drag.moved = true;
      state.suppressClick = true;
      state.viewFitted = false;
      capturePointer(event.pointerId);
    }
    applyView();
  }
});

function endPointer(event) {
  state.pointers.delete(event.pointerId);
  if (state.pointers.size < 2) state.pinch = null;
  if (state.pointers.size === 0) {
    mapContainer.classList.remove("dragging");
    state.drag = null;
    setTimeout(() => { state.suppressClick = false; }, 0);
  }
}
mapContainer.addEventListener("pointerup", endPointer);
mapContainer.addEventListener("pointercancel", endPointer);
window.addEventListener("resize", () => {
  if (state.viewMode === "game" && state.svg) fitMap();
});

function phaseLabel(phase) {
  const labels = {
    lobby: "Lobby", setup_claim: "Claim", setup_place: "Place armies",
    setup_leaders: "Leaders", reinforce: "Reinforce", attack: "Attack",
    conquest_move: "Conquest", fortify: "Fortify", finished: "Finished"
  };
  return labels[phase] || phase;
}

function playerName(seat) {
  const game = state.game;
  if (!game || !game.players || seat < 0 || seat >= game.players.length) return "nobody";
  return game.players[seat].user;
}

function playerFaction(seat) {
  const game = state.game;
  if (!game || !game.players || seat < 0 || seat >= game.players.length) return 0;
  return game.players[seat].faction;
}

function factionHex(seat) {
  return factionColour(playerFaction(seat));
}

function isValidAttack(from, to) {
  const legal = state.game && state.game.legal;
  return !!(legal && legal.attacks.some((a) => a.from === from && a.to === to));
}

function isValidFortify(from, to) {
  const legal = state.game && state.game.legal;
  return !!(legal && legal.fortifies.some((f) => f.from === from && f.to === to));
}

function attackPair(from, to) {
  const legal = state.game && state.game.legal;
  return legal ? legal.attacks.find((a) => a.from === from && a.to === to) : null;
}

function territoryClick(index) {
  const game = state.game;
  if (!game || !game.legal || game.status !== "running") return;
  if (state.replayActive) return;
  if (state.pendingLeader) {
    if (game.legal.recruitPlaceable.includes(index)) {
      if (state.pendingLeader.type === "paid") {
        act("recruit_leader", { t: index });
      } else {
        act("play_card", { card: state.pendingLeader.slot, t: index });
      }
    }
    return;
  }
  const legal = game.legal;
  const printed = game.printed || {};
  if (legal.freeLeaderPending) {
    if (legal.leaderPlaceable.includes(index)) act("place_leader", { t: index });
    return;
  }
  if (printed.bonus > 0) {
    if ((legal.bonusPlaceable || []).includes(index)) {
      state.pendingPlace = { type: "place", territory: index, max: printed.bonus, bonus: true };
      renderGame();
    }
    return;
  }
  if (state.cardPlay) {
    cardPlayClick(index);
    return;
  }
  if (game.phase === "setup_claim") {
    if (legal.claimable.includes(index)) act("claim", { t: index });
    return;
  }
  if (game.phase === "setup_place") {
    if (legal.batchRemaining > 0 && legal.placeable.includes(index)) {
      state.pendingPlace = { type: "place", territory: index, max: legal.batchRemaining };
      renderGame();
    }
    return;
  }
  if (game.phase === "setup_leaders") {
    if (legal.leaderPlaceable.includes(index)) act("place_leader", { t: index });
    return;
  }
  if (game.phase === "reinforce") {
    if (legal.reinforcementsPending > 0 && legal.placeable.includes(index)) {
      state.pendingPlace = { type: "place", territory: index, max: legal.reinforcementsPending };
      renderGame();
    }
    return;
  }
  if (game.phase === "attack") {
    if (state.selectedFrom === null) {
      if (legal.attacks.some((a) => a.from === index)) {
        state.selectedFrom = index;
        state.selectedTo = null;
        renderGame();
      }
      return;
    }
    if (isValidAttack(state.selectedFrom, index)) {
      state.selectedTo = index;
      renderGame();
      return;
    }
    if (legal.attacks.some((a) => a.from === index)) {
      state.selectedFrom = index;
      state.selectedTo = null;
      renderGame();
    }
    return;
  }
  if (game.phase === "fortify") {
    if (state.selectedFrom === null) {
      if (legal.fortifies.some((f) => f.from === index)) {
        state.selectedFrom = index;
        state.selectedTo = null;
        renderGame();
      }
      return;
    }
    if (isValidFortify(state.selectedFrom, index)) {
      state.selectedTo = index;
      renderGame();
      return;
    }
    if (legal.fortifies.some((f) => f.from === index)) {
      state.selectedFrom = index;
      state.selectedTo = null;
      renderGame();
    }
  }
}

/* ---------------------------------------------------------------- */
/* Side tabs: Players · Cards · Log · Chat (PLAN.md 8.11)            */
/* ---------------------------------------------------------------- */

const tabs = { active: "players", open: false, logSeen: null, chatUnread: 0 };
const dockQuery = matchMedia("(max-width: 999px) and (orientation: portrait)");

try {
  const saved = localStorage.getItem("friskTab");
  if (["players", "cards", "log", "chat"].includes(saved)) tabs.active = saved;
} catch (error) { /* storage unavailable */ }

/* Portrait phones/tablets dock the controls at the bottom and show a tab as a
 * slide-up sheet; wider screens keep one tab open in the side column. */
function dockLayout() {
  return dockQuery.matches;
}

function tabVisible(name) {
  return state.viewMode === "game" && tabs.active === name &&
    (!dockLayout() || tabs.open) && document.visibilityState === "visible";
}

function selectTab(name) {
  if (dockLayout()) {
    tabs.open = !(tabs.open && tabs.active === name);
  }
  tabs.active = name;
  try { localStorage.setItem("friskTab", name); } catch (error) { /* ignore */ }
  applyTabs();
  if (name === "chat" && tabVisible("chat")) {
    const log = $("game-chat-log");
    log.scrollTop = log.scrollHeight;
  }
}

function applyTabs() {
  const docked = dockLayout();
  document.querySelectorAll("#side-tabs .tab").forEach((button) => {
    const on = button.dataset.tab === tabs.active && (!docked || tabs.open);
    button.classList.toggle("active", on);
    button.setAttribute("aria-pressed", on ? "true" : "false");
  });
  document.querySelectorAll(".tab-panel").forEach((panel) => {
    panel.classList.toggle("active", panel.dataset.tab === tabs.active);
  });
  $("game-side").classList.toggle("sheet-open", docked && tabs.open);
  updateTabBadges();
}

function setBadge(id, text, urgent) {
  const badge = $(id);
  badge.textContent = text;
  badge.classList.toggle("hidden", !text);
  badge.classList.toggle("urgent", !!urgent);
}

function updateTabBadges() {
  const game = state.game;
  if (game) {
    if (tabs.logSeen === null || tabVisible("log")) tabs.logSeen = game.version;
    const unread = (game.log || []).filter((event) => event.version > tabs.logSeen).length;
    setBadge("badge-log", unread ? (unread > 9 ? "9+" : String(unread)) : "");
    const cards = (game.hand || []).length + (game.adventureHand || []).length;
    const flags = game.flags || {};
    const urgent = game.you >= 0 && game.current === game.you &&
      (flags.mustTrade || flags.mustDiscardAdventure);
    setBadge("badge-cards", urgent ? "!" : cards ? String(cards) : "", urgent);
  }
  if (tabVisible("chat")) tabs.chatUnread = 0;
  setBadge("badge-chat", tabs.chatUnread ? (tabs.chatUnread > 9 ? "9+" : String(tabs.chatUnread)) : "", true);
}

document.querySelectorAll("#side-tabs .tab").forEach((button) => {
  button.addEventListener("click", () => selectTab(button.dataset.tab));
});
dockQuery.addEventListener("change", () => {
  tabs.open = false;
  applyTabs();
  refitIfFitted();
});

/* Territories the current card-play step accepts. */
function cardPlayTargets() {
  const game = state.game;
  const play = state.cardPlay;
  const me = game.you;
  const out = [];
  const frozen = new Set(((game.printed || {}).frozen || []).map(([t]) => t));
  game.territories.forEach((t, i) => {
    if (play.effect === "MOUNTAIN_PASS") {
      if (play.from === null) {
        if (t.owner === me && t.armies >= 2 && !frozen.has(i) &&
            game.territories.some((u, j) => u.owner !== me && u.armies > 0 && !frozen.has(j) && sameRange(i, j))) out.push(i);
      } else if (t.owner !== me && t.armies > 0 && !frozen.has(i) && sameRange(play.from, i)) {
        out.push(i);
      }
    } else if (play.from === null) {
      if (t.leader === me + 1) out.push(i);
    } else if (t.owner === me && t.leader === 0 && i !== play.from) {
      out.push(i);
    }
  });
  return out;
}

async function cardPlayClick(index) {
  const play = state.cardPlay;
  if (!cardPlayTargets().includes(index)) return;
  if (play.from === null) {
    play.from = index;
    state.selectedFrom = index;
    renderGame();
    return;
  }
  const from = play.from;
  state.cardPlay = null;
  const ok = await act("play_card", { card: play.slot, from, to: index });
  if (ok && play.effect === "MOUNTAIN_PASS") {
    state.selectedFrom = from;   /* the invasion is now open: pick dice */
    state.selectedTo = index;
    renderGame();
  } else if (!ok) {
    renderGame();
  }
}

function renderGame() {
  const game = state.game;
  if (!game) return;
  $("game-view").dataset.version = String(game.version);
  $("resign-game").classList.toggle("hidden",
    !(game.status === "running" && game.you >= 0 && !state.replayActive &&
      ["reinforce", "attack", "fortify"].includes(game.phase)));
  $("game-name").textContent = game.name;
  $("game-status").textContent = game.status;
  $("game-turn").textContent = game.status === "finished"
    ? (game.winner >= 0 ? `${playerName(game.winner)} wins` : "Finished")
    : `Turn ${game.turn} · ${playerName(game.current)} · ${phaseLabel(game.phase)}`;
  renderPhaseBar();
  renderPlayers();
  renderActions();
  renderHand();
  renderAdventure();
  renderLog();
  renderMap();
  updateTabBadges();
  updateTitle();
  updateMusic();
  updateChatControls(chatViews.game);
  maybeNotifyTurn();
}

function renderPhaseBar() {
  const bar = $("phase-bar");
  bar.replaceChildren();
  const game = state.game;
  const phases = [
    ["setup_claim", "Setup"], ["setup_place", "Setup"], ["setup_leaders", "Setup"],
    ["reinforce", "Reinforce"], ["attack", "Attack"],
    ["conquest_move", "Attack"], ["fortify", "Fortify"]
  ];
  let lastLabel = "";
  for (const [phase, label] of phases) {
    if (label === lastLabel) continue;
    lastLabel = label;
    const span = document.createElement("span");
    span.className = "step";
    span.textContent = label;
    const active = game.phase === phase ||
      (label === "Setup" && phase.startsWith("setup") && game.phase.startsWith("setup")) ||
      (label === "Attack" && (game.phase === "attack" || game.phase === "conquest_move"));
    span.classList.toggle("active", active);
    bar.append(span);
  }
}

function renderPlayers() {
  const box = $("game-players");
  box.replaceChildren();
  const game = state.game;
  const kick = game.kick;
  const votePhase = game.status === "running" && game.you >= 0 &&
    game.players[game.you] && game.players[game.you].alive &&
    ["reinforce", "attack", "fortify"].includes(game.phase);
  for (let seat = 0; seat < game.players.length; seat++) {
    const player = game.players[seat];
    const row = document.createElement("div");
    row.className = "player-row" + (seat === game.current ? " current" : "");
    row.append(dot(player.faction));
    const name = document.createElement("span");
    name.className = "pname";
    name.textContent = player.user + (seat === game.you ? " (you)" : "");
    const detail = document.createElement("span");
    detail.className = "detail";
    if (game.status === "open") {
      detail.textContent = "waiting";
    } else {
      const territoryCount = game.territories.filter((t) => t.owner === seat).length;
      detail.textContent = `${territoryCount} land · ${player.leaders} leaders` +
        (player.alive ? "" : " · out");
    }
    row.append(name, detail);
    if (votePhase && !kick && seat !== game.you && player.alive) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "kick";
      button.textContent = "Kick";
      button.title = "Vote to remove a player who has been idle for a week";
      button.addEventListener("click", () => voteKick(seat));
      row.append(button);
    }
    box.append(row);
  }
  if (kick && kick.target >= 0 && game.players[kick.target]) {
    const status = document.createElement("div");
    status.className = "kick-vote";
    const label = document.createElement("span");
    const votes = kick.votes || [];
    label.append(document.createTextNode("Inactivity kick vote: "));
    const who = document.createElement("strong");
    who.textContent = game.players[kick.target].user;
    label.append(who, document.createTextNode(` — ${votes.length}/${kick.needed} votes`));
    status.append(label);
    if (game.you >= 0 && kick.target !== game.you && !votes.includes(game.you) &&
        game.players[game.you] && game.players[game.you].alive) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "secondary tiny";
      button.textContent = "Vote to remove";
      button.addEventListener("click", () => voteKick(kick.target));
      status.append(button);
    }
    box.append(status);
  }
  if (game.status !== "open") renderRegions(box);
}

/* Each region with its bonus and who holds it (or how much of it you hold),
 * so the "+N regions" in the reinforcement breakdown can be read off. */
function renderRegions(box) {
  const game = state.game;
  const meta = state.mapMeta;
  if (!meta || !(meta.regions || []).length) return;
  const heading = document.createElement("h3");
  heading.textContent = "Regions";
  box.append(heading);
  const list = document.createElement("div");
  list.className = "regions";
  const regions = meta.regions.slice().sort((a, b) => b.bonus - a.bonus || a.name.localeCompare(b.name));
  for (const region of regions) {
    const members = meta.territories
      .map((def, index) => (def.region === region.id ? index : -1))
      .filter((index) => index >= 0 && game.territories[index]);
    const owners = new Set(members.map((index) => game.territories[index].owner));
    const holder = owners.size === 1 ? [...owners][0] : -1;
    const row = document.createElement("div");
    row.className = "region-row" + (holder >= 0 && holder === game.you ? " mine" : "");
    const name = document.createElement("span");
    name.className = "rname";
    name.textContent = `${region.name} +${region.bonus}`;
    const detail = document.createElement("span");
    detail.className = "detail";
    if (holder >= 0 && !game.territories[members[0]].neutral) {
      detail.append(dot(playerFaction(holder)), document.createTextNode(` ${playerName(holder)}`));
    } else if (game.you >= 0) {
      const mine = members.filter((index) => game.territories[index].owner === game.you).length;
      detail.textContent = `you ${mine}/${members.length}`;
    } else {
      detail.textContent = `${members.length} lands`;
    }
    row.append(name, detail);
    list.append(row);
  }
  box.append(list);
}

async function voteKick(target) {
  if (!state.game || !state.game.kick) {
    if (!confirm(`Start a kick vote against ${playerName(target)}? This is only allowed after a week of inactivity.`)) {
      return;
    }
  }
  const result = await post("/api/game/act",
    { g: state.gameId, type: "kick_vote", target });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  toast("Kick vote recorded.");
  await loadGame(true);
}

function addButton(parent, label, handler, cls) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = cls || "";
  button.textContent = label;
  button.addEventListener("click", handler);
  parent.append(button);
  return button;
}

function addHint(parent, text) {
  const node = document.createElement("p");
  node.className = "hint";
  node.textContent = text;
  parent.append(node);
}

/* Slider for any count between min and max plus a confirm button; quick
 * buttons for the usual amounts are added by the caller. */
function countPicker(parent, { min, max, initial, verb, onPick }) {
  const row = document.createElement("div");
  row.className = "count-picker";
  const go = document.createElement("button");
  go.type = "button";
  go.dataset.confirm = "1";
  let value = Math.max(min, Math.min(max, initial));
  const sync = () => { go.textContent = `${verb} ${value}`; };
  if (max > min) {
    const range = document.createElement("input");
    range.type = "range";
    range.min = String(min);
    range.max = String(max);
    range.step = "1";
    range.value = String(value);
    range.setAttribute("aria-label", `${verb}: how many armies`);
    range.addEventListener("input", () => { value = Number(range.value); sync(); });
    row.append(range);
  }
  sync();
  go.addEventListener("click", () => onPick(value));
  row.append(go);
  parent.append(row);
}

function quickRow(parent) {
  const row = document.createElement("div");
  row.className = "quick";
  parent.append(row);
  return row;
}

function placeControls(parent, max, type, territory) {
  addHint(parent, `Place armies in ${mapName(territory)}.`);
  const place = async (count) => {
    state.pendingPlace = null;
    await act(type, { t: territory, count });
  };
  countPicker(parent, { min: 1, max, initial: max, verb: "Place", onPick: place });
  const quick = quickRow(parent);
  for (const count of [1, 5].filter((n) => n < max)) {
    addButton(quick, String(count), () => place(count), "secondary");
  }
  addButton(quick, "Cancel", () => {
    state.pendingPlace = null;
    renderGame();
  }, "secondary");
}

function canBringLeader(from, to) {
  const game = state.game;
  return game && game.you >= 0 &&
    game.territories[from] && game.territories[to] &&
    game.territories[from].leader === game.you + 1 &&
    game.territories[to].leader === 0;
}

function addLeaderToggle(parent, from, to) {
  if (!canBringLeader(from, to)) return;
  const label = document.createElement("label");
  label.className = "leader-toggle";
  const input = document.createElement("input");
  input.type = "checkbox";
  input.checked = state.bringLeader;
  input.addEventListener("change", () => { state.bringLeader = input.checked; });
  label.append(input, document.createTextNode("Bring the leader"));
  parent.append(label);
}

function renderActions() {
  const box = $("game-actions");
  box.replaceChildren();
  const game = state.game;
  if (!game) return;
  if (state.replayActive) {
    addHint(box, "Replaying what happened while you were away…");
    return;
  }
  if (state.pendingLeader) {
    addHint(box, "Click one of the highlighted territories for the new leader.");
    addButton(box, "Cancel", () => {
      state.pendingLeader = null;
      renderGame();
    }, "secondary");
    return;
  }
  if (game.flags && game.flags.mustDiscardAdventure &&
      game.current === game.you) {
    addHint(box, "Discard adventure cards down to four before continuing.");
    return;
  }
  if (game.status === "open") { renderOpenRoom(box); return; }
  if (game.status === "finished") {
    addHint(box, game.winner >= 0 ? `${playerName(game.winner)} wins the game.` : "The game is over.");
    addButton(box, "Back to lobby", showLobby, "secondary");
    return;
  }
  if (game.you < 0) {
    addHint(box, "Spectating. You can read the board and log.");
    return;
  }
  const legal = game.legal;
  if (!legal) {
    addHint(box, `Waiting for ${playerName(game.current)}…`);
    return;
  }
  const printed = game.printed || {};
  const effects = [];
  if (printed.toll >= 0) effects.push(`Corsairs of Umbar: seaborne invasions lose 3 battalions first (until ${playerName(printed.toll)}'s turn)`);
  if ((printed.frozen || []).length) effects.push(`Winter Storms: no invasions into or out of ${printed.frozen.map(([t]) => mapName(t)).join(", ")}`);
  if ((printed.blockedBridges || []).length) effects.push(`Blocked bridges (Leader needed): ${printed.blockedBridges.map(([b]) => bridgeName(b)).join(", ")}`);
  for (const text of effects) {
    const note = document.createElement("p");
    note.className = "hint effect-note";
    note.textContent = text;
    box.append(note);
  }
  if (legal.freeLeaderPending) {
    addHint(box, "Appoint a Second Leader: click one of your territories for the new Leader.");
    return;
  }
  if (printed.bonus > 0) {
    if (state.pendingPlace && state.pendingPlace.bonus) {
      const t = state.pendingPlace.territory;
      addHint(box, `Mission battalions for ${mapName(t)}.`);
      const place = (count) => { state.pendingPlace = null; act("place", { t, count }); };
      countPicker(box, { min: printed.bonusSplit ? 1 : printed.bonus, max: printed.bonus,
        initial: printed.bonus, verb: "Place", onPick: place });
      addButton(quickRow(box), "Cancel", () => { state.pendingPlace = null; renderGame(); }, "secondary");
    } else {
      addHint(box, `Mission complete: place ${printed.bonus} battalions — click a highlighted territory${printed.bonusSplit ? "" : " (all in one)"}.`);
    }
    return;
  }
  if (state.cardPlay) {
    const play = state.cardPlay;
    addHint(box, play.effect === "MOUNTAIN_PASS"
      ? (play.from === null ? "The Way Is Under the Mountains: click your territory next to a mountain range."
        : `Now click an enemy territory next to the same range as ${mapName(play.from)}.`)
      : (play.from === null ? "Borne to Another Place: click the territory with the Leader to move."
        : `Now click your territory to move the Leader from ${mapName(play.from)} to.`));
    addButton(box, "Cancel", () => { state.cardPlay = null; renderGame(); }, "secondary");
    return;
  }
  if (game.flags && game.flags.mustTrade) {
    addHint(box, "You have five or more territory cards and must trade a set.");
    const set = findSet(game.hand);
    if (set) addButton(box, "Trade a set", () => act("trade_cards", { cardA: set[0], cardB: set[1], cardC: set[2] }));
    return;
  }
  if (game.phase === "setup_claim") {
    addHint(box, "Click a highlighted territory to claim it.");
    return;
  }
  if (game.phase === "setup_place") {
    addHint(box, `${legal.setupPool} armies left; click a highlighted territory.`);
    if (state.pendingPlace) placeControls(box, state.pendingPlace.max, "place", state.pendingPlace.territory);
    return;
  }
  if (game.phase === "setup_leaders") {
    addHint(box, `Click a highlighted territory to place a leader (${legal.leadersRemaining} left).`);
    return;
  }
  if (game.phase === "reinforce") {
    const info = game.reinforce || {};
    addHint(box, `${info.base || 0} base + ${info.regions || 0} regions + ${info.strongholds || 0} strongholds + ${info.cards || 0} cards = ${info.total || 0}; ${legal.reinforcementsPending} left to place.`);
    if (legal.canTrade) {
      const set = findSet(game.hand);
      if (set) addButton(box, "Trade a set", () => act("trade_cards", { cardA: set[0], cardB: set[1], cardC: set[2] }), "secondary");
    }
    if (legal.canRecruitLeader) {
      addButton(box, `Recruit leader (${legal.recruitCost} armies)`, () => {
        state.pendingLeader = { type: "paid" };
        renderGame();
      }, "secondary");
    }
    if (legal.reinforcementsPending > 0) {
      if (state.pendingPlace) placeControls(box, state.pendingPlace.max, "place", state.pendingPlace.territory);
      else addHint(box, "Click a highlighted territory to place armies.");
    } else if (legal.canDoneReinforce) {
      const done = addButton(box, "Done reinforcing", () => act("done_reinforce", {}));
      done.dataset.confirm = "1";
    }
    return;
  }
  if (game.phase === "attack") {
    const pair = attackPair(state.selectedFrom, state.selectedTo);
    if (!pair) {
      addHint(box, state.selectedFrom === null
        ? "Click one of your territories with two or more armies."
        : "Click a highlighted enemy territory to attack.");
    } else {
      const from = state.selectedFrom;
      const to = state.selectedTo;
      const own = game.territories[from].armies;
      addHint(box, `Attack ${mapName(to)} (${game.territories[to].armies}) from ${mapName(from)} (${own}).`);
      const diceRow = quickRow(box);
      for (let dice = 1; dice <= pair.maxDice; dice++) {
        addButton(diceRow, `${dice} ${dice === 1 ? "die" : "dice"}`, () => act("attack", { from, to, dice }));
      }
      /* Blitz repeats max-dice rounds until the target falls or the source is
       * down to the chosen number of armies (PLAN.md 8.6 "stop at N"). */
      const blitzRow = quickRow(box);
      const stop = document.createElement("select");
      stop.className = "stop-at";
      stop.setAttribute("aria-label", "Blitz stops when the attacking territory is down to");
      const stops = [1, 2, 3, 5, 10].filter((n) => n < own);
      for (const n of stops) {
        const option = document.createElement("option");
        option.value = String(n);
        option.textContent = `stop at ${n}`;
        stop.append(option);
      }
      stop.value = String(stops.filter((n) => n <= state.blitzStop).pop() || 1);
      stop.addEventListener("change", () => { state.blitzStop = Number(stop.value); });
      addButton(blitzRow, "Blitz", () => act("attack", {
        from, to, dice: pair.maxDice, repeat: 1, stopAt: Number(stop.value)
      }), "secondary");
      blitzRow.append(stop);
      addButton(blitzRow, "Clear", () => {
        state.selectedFrom = state.selectedTo = null;
        renderGame();
      }, "secondary");
      /* Printed cards that act on this invasion. */
      const inv = printed.invasion;
      if (inv && inv.from === from && inv.to === to && (inv.noStronghold || inv.bonus)) {
        addHint(box, [inv.noStronghold ? "the stronghold's +1 is ignored" : "",
          inv.bonus ? `+${inv.bonus} on your highest die` : ""].filter(Boolean).join(" · ") + " for this invasion.");
      }
      const cards = quickRow(box);
      (game.adventureHand || []).forEach((card, slot) => {
        if (PAIR_CARDS.includes(card.effect) && (legal.playableAdventure || []).includes(slot) &&
            pairCardFits(card.effect, from, to)) {
          addButton(cards, `Play ${card.title}`, () => act("play_card", { card: slot, from, to }), "secondary");
        }
      });
      const mine = game.players[game.you];
      if (legal.canOathbreakers && mine && mine.oathbreakers > 0) {
        addButton(cards, `Send ${mine.oathbreakers} Oathbreakers`, () => act("oathbreakers", { t: from }), "secondary");
      }
    }
    const done = addButton(box, "Done attacking", () => act("done_attack", {}), "secondary");
    done.dataset.confirm = "1";
    return;
  }
  if (game.phase === "conquest_move") {
    const pending = game.pending || {};
    addHint(box, `Move ${pending.min}–${pending.max} armies into ${mapName(pending.to)}.`);
    addLeaderToggle(box, pending.from, pending.to);
    const move = (count) => act("move_in", {
      from: pending.from, to: pending.to, count,
      leader: state.bringLeader ? 1 : 0
    });
    countPicker(box, { min: pending.min, max: pending.max, initial: pending.max, verb: "Move", onPick: move });
    if (pending.max > pending.min) addButton(quickRow(box), `Min (${pending.min})`, () => move(pending.min), "secondary");
    return;
  }
  if (game.phase === "fortify") {
    if (!game.flags.fortified && state.selectedFrom !== null && state.selectedTo !== null) {
      const max = Math.max(1, game.territories[state.selectedFrom].armies - 1);
      addHint(box, `Move armies from ${mapName(state.selectedFrom)} to ${mapName(state.selectedTo)}.`);
      const route = (legal.fortifies || []).find((f) => f.from === state.selectedFrom && f.to === state.selectedTo);
      if (route && route.needsLeader) {
        state.bringLeader = true;
        addHint(box, "A blocked bridge is on the way: the Leader goes along.");
      }
      addLeaderToggle(box, state.selectedFrom, state.selectedTo);
      const from = state.selectedFrom;
      const to = state.selectedTo;
      const fortify = (count) => act("fortify", {
        from, to, count, leader: state.bringLeader ? 1 : 0
      });
      countPicker(box, { min: 1, max, initial: max, verb: "Move", onPick: fortify });
      const quick = quickRow(box);
      if (max > 1) addButton(quick, "1", () => fortify(1), "secondary");
      addButton(quick, "Clear", () => {
        state.selectedFrom = state.selectedTo = null;
        renderGame();
      }, "secondary");
    } else if (!game.flags.fortified) {
      addHint(box, "Click a territory with two or more armies, then a connected territory.");
    } else {
      addHint(box, "You have fortified this turn.");
    }
    const end = addButton(box, "End turn", () => act("end_turn", {}));
    end.dataset.confirm = "1";
    return;
  }
}

function renderFactionPicker(box) {
  const game = state.game;
  const factions = (state.mapMeta && state.mapMeta.factions) || [];
  const seated = game.you >= 0;
  const full = game.playerCount >= game.maxPlayers;
  addHint(box, seated ? "Your faction (pick a free one to switch):" : "Pick a faction to join:");
  const list = document.createElement("div");
  list.className = "faction-pick";
  factions.forEach((faction, index) => {
    const holder = game.players.find((player) => player.faction === index);
    const mine = seated && holder && holder.seat === game.you;
    const button = document.createElement("button");
    button.type = "button";
    button.className = "secondary faction-choice" + (mine ? " mine" : "");
    button.append(dot(index), document.createTextNode(` ${faction.name || `Faction ${index + 1}`}`));
    if (holder) {
      const who = document.createElement("span");
      who.className = "muted small";
      who.textContent = mine ? " (you)" : ` (${holder.user})`;
      button.append(who);
    }
    button.disabled = !!holder || (!seated && full);
    button.addEventListener("click", () => chooseFaction(index));
    list.append(button);
  });
  box.append(list);
}

async function chooseFaction(faction) {
  const game = state.game;
  const fields = { g: game.id, faction };
  if (game.you < 0 && game.hasPassword) {
    const entered = prompt("Game password:");
    if (entered === null) return;
    fields.password = entered;
  }
  const result = await post("/api/game/join", fields);
  if (!result.ok) { toast(errorMessage(result), true); }
  loadGame(true);
}

function renderOpenRoom(box) {
  const game = state.game;
  addHint(box, `${game.playerCount} of ${game.maxPlayers} seats taken.`);
  if (state.mapMeta && (state.mapMeta.factions || []).length) renderFactionPicker(box);
  if (game.you >= 0) {
    const me = game.players[game.you];
    if (me && me.user === game.host) {
      const button = addButton(box, "Start game", () => startGame(game.id));
      button.dataset.confirm = "1";
      button.disabled = game.playerCount < 2;
      if (game.playerCount < 2) addHint(box, "At least two players are needed.");
    }
    addButton(box, "Leave", async () => {
      const result = await post("/api/game/leave", { g: game.id });
      if (!result.ok) { toast(errorMessage(result), true); return; }
      showLobby();
    }, "secondary");
  } else {
    addButton(box, "Join this game", async () => {
      let password = "";
      if (game.hasPassword) {
        const entered = prompt("Game password:");
        if (entered === null) return;
        password = entered;
      }
      const result = await post("/api/game/join", { g: game.id, password });
      if (!result.ok) { toast(errorMessage(result), true); return; }
      loadGame(true);
    });
  }
}

function mapName(index) {
  const meta = state.mapMeta;
  if (meta && meta.territories && meta.territories[index]) return meta.territories[index].name;
  return `#${index}`;
}

function findSet(hand) {
  if (!hand || hand.length < 3) return null;
  const wilds = hand.filter((card) => card.wild);
  const plain = hand.filter((card) => !card.wild);
  if (wilds.length >= 3) return wilds.slice(0, 3).map((card) => card.card);
  if (wilds.length >= 2 && plain.length >= 1) {
    return [wilds[0].card, wilds[1].card, plain[0].card];
  }
  if (wilds.length === 1 && plain.length >= 2) {
    return [wilds[0].card, plain[0].card, plain[1].card];
  }
  const bySymbol = { A: [], B: [], C: [] };
  for (const card of plain) {
    if (bySymbol[card.symbol]) bySymbol[card.symbol].push(card);
  }
  for (const symbol of ["A", "B", "C"]) {
    if (bySymbol[symbol].length >= 3) return bySymbol[symbol].slice(0, 3).map((card) => card.card);
  }
  if (bySymbol.A.length && bySymbol.B.length && bySymbol.C.length) {
    return [bySymbol.A[0].card, bySymbol.B[0].card, bySymbol.C[0].card];
  }
  return null;
}

function renderHand() {
  const box = $("game-hand");
  box.replaceChildren();
  const game = state.game;
  if (!game.hand || game.hand.length === 0) {
    const empty = document.createElement("p");
    empty.className = "small muted";
    empty.textContent = game.you < 0 ? "Spectating." : "No territory cards yet.";
    box.append(empty);
    return;
  }
  for (const card of game.hand) {
    const tile = document.createElement("div");
    tile.className = "card";
    const symbol = document.createElement("div");
    symbol.className = "sym";
    symbol.textContent = card.wild ? "★ wild" : card.symbol;
    const name = document.createElement("div");
    name.className = "tname";
    name.textContent = card.wild ? "Wild card" : mapName(card.territory);
    tile.append(symbol, name);
    box.append(tile);
  }
  const set = findSet(game.hand);
  if (set && game.legal && (game.legal.canTrade || (game.flags && game.flags.mustTrade))) {
    const trade = document.createElement("button");
    trade.type = "button";
    trade.className = "secondary tiny";
    trade.textContent = "Trade a set";
    trade.addEventListener("click", () => act("trade_cards", { cardA: set[0], cardB: set[1], cardC: set[2] }));
    box.append(trade);
  }
}

function territoryIndex(name) {
  const meta = state.mapMeta;
  if (!meta || !meta.territories) return -1;
  return meta.territories.findIndex((t) => t.name === name);
}

function missionProgress(card) {
  const game = state.game;
  const meta = state.mapMeta;
  if (!game || !meta || !card.args) return null;
  const args = card.args;
  if (card.effect === "HOLD_TERRITORIES") {
    const names = args.territories || [];
    const owned = names.filter((name) => {
      const i = territoryIndex(name);
      return i >= 0 && game.territories[i].owner === game.you;
    }).length;
    return { text: `${owned}/${names.length} held`, met: names.length > 0 && owned === names.length };
  }
  if (card.effect === "HOLD_REGION") {
    const region = (meta.regions || []).find((r) => r.name === args.region);
    if (!region) return null;
    let total = 0;
    let owned = 0;
    meta.territories.forEach((t, i) => {
      if (t.region === region.id) {
        total++;
        if (game.territories[i].owner === game.you) owned++;
      }
    });
    return { text: `${owned}/${total} of ${region.name}`, met: total > 0 && owned === total };
  }
  if (card.effect === "CONQUER_N_THIS_TURN") {
    const n = args.n || 1;
    const count = game.conqueredThisTurn || 0;
    return { text: `${count}/${n} this turn`, met: count >= n };
  }
  if (card.effect === "LEADER_REACH") {
    const i = territoryIndex(args.territory);
    const met = i >= 0 && game.territories[i].owner === game.you &&
      game.territories[i].leader === game.you + 1;
    return { text: met ? `leader at ${args.territory}` : `bring a leader to ${args.territory}`, met };
  }
  if (card.effect === "HOLD_STRONGHOLDS") {
    const n = args.n || 1;
    let held = 0;
    meta.territories.forEach((t, i) => {
      if (t.stronghold && game.territories[i].owner === game.you) held++;
    });
    return { text: `${held}/${n} strongholds`, met: held >= n };
  }
  return null;
}

/* Printed cards (PLAN.md 10b) -------------------------------------------- */
const DEFENDER_CARDS = ["DEFENSE_BONUS", "REROLL", "CANCEL_EVENT",
  "INVADED_REINFORCE", "WITHDRAW", "STORM_REPEL"];
const PAIR_CARDS = ["AMBUSH", "GRIMA", "NO_STRONGHOLD", "SEABORNE_BONUS"];

function cardMeta(card) {
  const meta = state.mapMeta;
  return meta && meta.adventureCards ? meta.adventureCards.find((c) => c.id === card.id) : null;
}

function cardArt(card) {
  const meta = cardMeta(card);
  return meta && meta.image ? `/maps/${encodeURIComponent(state.mapPack)}/${meta.image}` : null;
}

function showCardArt(url, title) {
  const viewer = $("card-viewer");
  const img = viewer.querySelector("img");
  img.src = url;
  img.alt = title;
  viewer.classList.remove("hidden");
}

function mySide() {
  const game = state.game;
  return game && game.you >= 0 && game.players[game.you] ? game.players[game.you].side : "";
}

function missionReward(card) {
  const args = card.args || {};
  const amount = mySide() === "evil" ? args.evil : args.good;
  switch (args.reward) {
    case "region": return `${amount} battalions anywhere in ${args.region}`;
    case "territory": return `${amount} battalions in ${args.territory}`;
    case "port": return `${amount} battalions in any 1 port`;
    case "any": return `${amount} battalions in any territory`;
    case "card": return "an extra Territory card at the end of the turn";
    case "oathbreakers": return `${amount} Oathbreakers for a later invasion`;
    default: return "";
  }
}

function isSeaborne(from, to) {
  const meta = state.mapMeta;
  if (!meta || !meta.territories[from]) return false;
  if ((meta.territories[from].adjacent || []).includes(to)) return false;
  return (meta.links || []).some((l) => (l.a === from && l.b === to) || (l.a === to && l.b === from));
}

function sameRange(a, b) {
  return ((state.mapMeta && state.mapMeta.mountains) || []).some((r) =>
    r.territories.includes(a) && r.territories.includes(b));
}

/* Whether a pair card fits the invasion selected on the map. */
function pairCardFits(effect, from, to) {
  const game = state.game;
  const meta = state.mapMeta;
  if (from === null || to === null || !attackPair(from, to)) return false;
  if (effect === "GRIMA") return game.territories[to].armies >= 2;
  if (effect === "NO_STRONGHOLD") return !!meta.territories[to].stronghold;
  if (effect === "SEABORNE_BONUS") return isSeaborne(from, to);
  return true;
}

function bridgeName(index) {
  const bridge = ((state.mapMeta && state.mapMeta.bridges) || [])[index];
  return bridge ? `${mapName(bridge.a)} – ${mapName(bridge.b)}` : `bridge ${index}`;
}

function addStandingSelect(parent, card, index) {
  const select = document.createElement("select");
  const options = card.effect === "CANCEL_EVENT"
    ? [[0, 0, "Set order: never"], [1, 0, "Cancel any event"]]
    : [[0, 0, "Use: never"], [2, 1, "Use: whenever attacked"],
       [1, 0, "Use: on a stronghold"],
       [2, 2, "Use: vs ≥2 attack dice"], [2, 3, "Use: vs 3 attack dice"]];
  const current = `${card.auto || 0}:${card.autoN || 0}`;
  let matched = false;
  for (const [mode, n, label] of options) {
    const option = document.createElement("option");
    option.value = `${mode}:${n}`;
    option.textContent = label;
    if (option.value === current) { option.selected = true; matched = true; }
    select.append(option);
  }
  if (!matched) {
    const option = document.createElement("option");
    option.value = current;
    option.textContent = card.auto ? `On ≥${card.autoN} dice` : "Never";
    option.selected = true;
    select.append(option);
  }
  select.addEventListener("change", () => {
    const [mode, n] = select.value.split(":").map(Number);
    setStanding(index, mode, n);
  });
  parent.append(select);
}

async function setStanding(index, mode, n) {
  const result = await post("/api/game/act",
    { g: state.gameId, type: "set_standing", card: index, mode, n });
  if (!result.ok) { toast(errorMessage(result), true); return; }
  await loadGame(true);
}

async function playAdventure(index, effect) {
  if (effect === "RECRUIT_LEADER") {
    state.pendingLeader = { type: "card", slot: index };
    renderGame();
    return;
  }
  if (PAIR_CARDS.includes(effect)) {
    if (!pairCardFits(effect, state.selectedFrom, state.selectedTo)) {
      toast("Select the invasion on the map first (your territory, then the target).", true);
      return;
    }
    await act("play_card", { card: index, from: state.selectedFrom, to: state.selectedTo });
    return;
  }
  if (effect === "MOUNTAIN_PASS" || effect === "LEADER_MOVE") {
    state.cardPlay = { slot: index, effect, from: null };
    state.selectedFrom = state.selectedTo = null;
    renderGame();
    return;
  }
  await act("play_card", { card: index });
}

async function discardAdventure(index) {
  await act("discard_adventure", { card: index });
}

function renderAdventure() {
  const box = $("game-adventure");
  box.replaceChildren();
  const game = state.game;
  if (!game || game.status === "open") return;
  if (game.you < 0 || !game.adventureHand) {
    const note = document.createElement("p");
    note.className = "small muted";
    note.textContent = game.you < 0 ? "Spectating." : "No adventure cards.";
    box.append(note);
    return;
  }
  const legal = game.legal || {};
  const playable = legal.playableAdventure || [];
  const mustDiscard = !!(game.flags && game.flags.mustDiscardAdventure &&
    game.current === game.you);
  if (game.adventureHand.length === 0) {
    const note = document.createElement("p");
    note.className = "small muted";
    note.textContent = "No adventure cards. Conquer a Site of Power with a Leader to draw one at the end of your turn.";
    box.append(note);
  }
  const printed = game.printed || {};
  if (printed.uprising >= 0) {
    const secret = document.createElement("p");
    secret.className = "small adv-progress";
    secret.textContent = `Local Uprising (secret): +4 battalions when you control ${mapName(printed.uprising)}.`;
    box.append(secret);
  }
  const mine = game.players[game.you];
  if (mine && mine.oathbreakers > 0) {
    const oath = document.createElement("p");
    oath.className = "small adv-progress";
    oath.textContent = `${mine.oathbreakers} Oathbreakers wait to join one of your invasions.`;
    box.append(oath);
  }
  game.adventureHand.forEach((card, index) => {
    const tile = document.createElement("div");
    tile.className = "adv-card";
    const kind = document.createElement("div");
    kind.className = "adv-kind";
    kind.textContent = card.kind;
    const title = document.createElement("div");
    title.className = "adv-title";
    title.textContent = card.title;
    const text = document.createElement("div");
    text.className = "adv-text";
    text.textContent = card.text;
    const art = cardArt(card);
    if (art) {
      const thumb = document.createElement("img");
      thumb.className = "adv-art";
      thumb.src = art;
      thumb.alt = card.title;
      thumb.loading = "lazy";
      thumb.title = "Show the card";
      thumb.addEventListener("click", () => showCardArt(art, card.title));
      tile.append(thumb);
    }
    tile.append(kind, title, text);
    if (card.effect === "SITE_MISSION") {
      const site = (card.args || {}).site;
      const at = territoryIndex(site);
      const there = at >= 0 && game.territories[at].owner === game.you &&
        game.territories[at].leader === game.you + 1;
      const line = document.createElement("div");
      line.className = "adv-progress";
      line.textContent = `${there ? "Leader at" : "Bring a Leader to"} ${site} · reward: ${missionReward(card)}`;
      tile.append(line);
    } else if (card.kind === "mission") {
      const progress = missionProgress(card);
      if (progress) {
        const line = document.createElement("div");
        line.className = "adv-progress";
        line.textContent = progress.met ? `Met: ${progress.text}` : `Progress: ${progress.text}`;
        tile.append(line);
      }
    }
    const actions = document.createElement("div");
    actions.className = "adv-actions";
    if (mustDiscard) {
      addButton(actions, "Discard", () => discardAdventure(index), "secondary tiny");
    } else if ((legal.claimableMissions || []).includes(index)) {
      addButton(actions, "Complete mission", () => act("claim_mission", { card: index }), "tiny");
    } else if (card.kind === "power") {
      if (DEFENDER_CARDS.includes(card.effect)) {
        addStandingSelect(actions, card, index);
      } else if (card.effect === "BRIDGE_BLOCK" && playable.includes(index)) {
        const pick = document.createElement("select");
        ((state.mapMeta && state.mapMeta.bridges) || []).forEach((bridge, b) => {
          const option = document.createElement("option");
          option.value = String(b);
          option.textContent = bridgeName(b);
          pick.append(option);
        });
        actions.append(pick);
        addButton(actions, "Block", () => act("play_card", { card: index, bridge: Number(pick.value) }), "tiny");
      } else if (playable.includes(index)) {
        addButton(actions, "Play", () => playAdventure(index, card.effect), "tiny");
      }
    }
    if (actions.children.length > 0) tile.append(actions);
    box.append(tile);
  });
  const counts = document.createElement("p");
  counts.className = "small muted";
  counts.textContent = `Adventure deck ${game.adventureDeck ?? "?"} · discard ${game.adventureDiscard ?? "?"}`;
  box.append(counts);
}

function renderLog() {
  const box = $("game-log");
  box.replaceChildren();
  const log = state.game.log || [];
  const closed = state.game.closedLinks || [];
  closed.forEach((link) => {
    const entry = document.createElement("div");
    entry.className = "entry";
    const kind = document.createElement("span");
    kind.className = "kind";
    kind.textContent = "sea_closed";
    entry.append(kind, document.createTextNode(
      ` · ${mapName(link.a)} ↔ ${mapName(link.b)} is closed`));
    box.append(entry);
  });
  if (log.length === 0 && closed.length === 0) {
    const empty = document.createElement("p");
    empty.className = "small muted";
    empty.textContent = "No events yet.";
    box.append(empty);
    return;
  }
  for (const event of log.slice().reverse()) {
    const entry = document.createElement("div");
    entry.className = "entry";
    const kind = document.createElement("span");
    kind.className = "kind";
    kind.textContent = event.kind;
    entry.append(kind, document.createTextNode(" · " + event.text));
    if (event.battles && event.battles.length) {
      const dice = document.createElement("div");
      dice.className = "dice-log";
      const faces = ["⚀", "⚁", "⚂", "⚃", "⚄", "⚅"];
      event.battles.forEach((battle) => {
        const line = document.createElement("div");
        const attacker = (battle.attacker || []).map((n) => faces[n - 1] || String(n)).join(" ");
        const defender = (battle.defender || []).map((n) => faces[n - 1] || String(n)).join(" ");
        const aBonus = battle.attackerBonus ? ` +${battle.attackerBonus}` : "";
        const dBonus = battle.defenderBonus ? ` +${battle.defenderBonus}` : "";
        line.textContent = `A ${attacker}${aBonus}  D ${defender}${dBonus}  −${battle.attackerLosses}/−${battle.defenderLosses}`;
        dice.append(line);
      });
      if (event.truncated) {
        const more = document.createElement("div");
        more.textContent = "…";
        dice.append(more);
      }
      entry.append(dice);
    }
    box.append(entry);
  }
}

function renderMap() {
  const game = state.game;
  const meta = state.mapMeta;
  if (!game || !meta || !state.svg) return;
  const legal = state.replayActive ? null : game.legal;
  const selectable = new Set();
  if (legal) {
    if (game.phase === "setup_claim") legal.claimable.forEach((t) => selectable.add(t));
    if (game.phase === "setup_place" && legal.batchRemaining > 0) legal.placeable.forEach((t) => selectable.add(t));
    if (game.phase === "reinforce" && legal.reinforcementsPending > 0) legal.placeable.forEach((t) => selectable.add(t));
    if (game.phase === "setup_leaders") legal.leaderPlaceable.forEach((t) => selectable.add(t));
    /* Before a source is chosen only sources glow; afterwards only its targets. */
    if (game.phase === "attack") {
      legal.attacks.forEach((a) => {
        if (state.selectedFrom === null) selectable.add(a.from);
        else if (a.from === state.selectedFrom) selectable.add(a.to);
      });
    }
    if (game.phase === "fortify" && !(game.flags && game.flags.fortified)) {
      legal.fortifies.forEach((f) => {
        if (state.selectedFrom === null) selectable.add(f.from);
        else if (f.from === state.selectedFrom) selectable.add(f.to);
      });
    }
    if (state.pendingLeader) {
      selectable.clear();
      legal.recruitPlaceable.forEach((t) => selectable.add(t));
    }
    const printed = game.printed || {};
    const override = legal.freeLeaderPending ? legal.leaderPlaceable
      : printed.bonus > 0 ? (legal.bonusPlaceable || [])
      : state.cardPlay ? cardPlayTargets() : null;
    if (override) {
      selectable.clear();
      override.forEach((t) => selectable.add(t));
    }
  }
  const territoryCount = Math.min(game.territories.length, meta.territories.length);
  const last = state.replayActive ? [] : lastActionTerritories(game);
  const source = state.selectedFrom !== null ? state.selectedFrom
    : state.pendingPlace ? state.pendingPlace.territory : state.replayFrom;
  const target = state.selectedTo !== null ? state.selectedTo : state.replayTo;
  state.svg.querySelectorAll("#hit-areas .hit").forEach((cell) => {
    cell.classList.toggle("selectable", selectable.has(Number(cell.getAttribute("data-t"))));
  });
  for (let i = 0; i < territoryCount; i++) {
    const path = state.svg.querySelector(`#t${i}`);
    if (!path) continue;
    const territory = game.territories[i];
    let fill = "none";
    let opacity = 0;
    if (territory.neutral) {
      fill = "url(#friskPatN)";
      opacity = 0.55;
    } else if (territory.owner >= 0) {
      fill = `url(#friskPat${territory.owner})`;
      opacity = 0.78;
    }
    path.style.fill = fill;
    path.style.fillOpacity = String(opacity);
    path.classList.toggle("selectable", selectable.has(i));
    path.classList.toggle("source", source === i);
    path.classList.toggle("target", target === i);
    path.classList.toggle("last-action", last.includes(i));
  }
  renderBadges({ selectable, source, target });
}

/* Territories an event points at; single-territory events only fill `to`. */
function eventTerritories(event) {
  const count = state.game ? state.game.territories.length : 0;
  const valid = (t) => Number.isInteger(t) && t >= 0 && t < count;
  const single = ["place", "place_leader", "claim"].includes(event.kind);
  return {
    from: !single && valid(event.from) ? event.from : null,
    to: valid(event.to) ? event.to : null
  };
}

function lastActionTerritories(game) {
  const log = game.log || [];
  for (let i = log.length - 1; i >= 0; i--) {
    if (!replaySpatial(log[i])) continue;
    const { from, to } = eventTerritories(log[i]);
    return [from, to].filter((t) => t !== null);
  }
  return [];
}

/* Piece sprites from the pack: factions[].sprites (unit1/unit3/unit5/leader)
 * plus pieces/stronghold.svg and pieces/site.svg by convention.  Each file is
 * copied once into the board's <defs> as a <symbol> (shape elements only, no
 * scripts or links) and drawn with <use>; the sprites paint with currentColor,
 * so one set serves every faction colour. */
const PIECE_SHAPES = new Set(["g", "path", "circle", "ellipse", "rect", "polygon", "polyline", "line"]);
const MARKER_SPRITES = { stronghold: "pieces/stronghold.svg", site: "pieces/site.svg" };

function copyShapes(source, target) {
  for (const child of source.children) {
    if (!PIECE_SHAPES.has(child.localName)) continue;
    const copy = document.createElementNS(NS, child.localName);
    for (const attribute of child.attributes) {
      if (/^on/i.test(attribute.name) || /href$/i.test(attribute.name)) continue;
      copy.setAttribute(attribute.name, attribute.value);
    }
    copyShapes(child, copy);
    target.append(copy);
  }
}

async function loadPieces(svg, meta, pack) {
  const files = new Set(Object.values(MARKER_SPRITES));
  for (const faction of (meta && meta.factions) || []) {
    for (const file of Object.values(faction.sprites || {})) {
      if (typeof file === "string") files.add(file);
    }
  }
  const defs = svg.querySelector("defs") ||
    svg.insertBefore(document.createElementNS(NS, "defs"), svg.firstChild);
  const symbols = {};
  let next = 0;
  await Promise.all([...files].map(async (file) => {
    if (!/^[A-Za-z0-9_][A-Za-z0-9_./-]*\.svg$/.test(file) || file.includes("..")) return;
    try {
      const response = await fetch(`/maps/${encodeURIComponent(pack)}/${file}`);
      if (!response.ok) return;
      const doc = new DOMParser().parseFromString(await response.text(), "image/svg+xml");
      const root = doc.documentElement;
      if (doc.querySelector("parsererror") || root.localName !== "svg") return;
      const symbol = document.createElementNS(NS, "symbol");
      const id = `frisk-piece-${next++}`;
      symbol.setAttribute("id", id);
      symbol.setAttribute("viewBox", root.getAttribute("viewBox") ||
        `0 0 ${root.getAttribute("width") || 24} ${root.getAttribute("height") || 24}`);
      copyShapes(root, symbol);
      defs.append(symbol);
      symbols[file] = id;
    } catch (error) { /* missing sprite: a text glyph is drawn instead */ }
  }));
  return symbols;
}

/* Greedy 5/3/1 split for the army tokens, at most five tokens; `more` marks a
 * stack too big to show (the number badge is always exact). */
function armyTokens(count) {
  const tokens = [];
  let left = count;
  for (const size of [5, 3, 1]) {
    while (left >= size && tokens.length < 5) {
      tokens.push(size);
      left -= size;
    }
  }
  return { tokens, more: left > 0 };
}

function svgNode(tag, attributes) {
  const node = document.createElementNS(NS, tag);
  for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
  return node;
}

function drawPiece(layer, file, x, y, size, colour, fallback) {
  const id = file && state.pieceSymbols ? state.pieceSymbols[file] : null;
  if (id) {
    const use = svgNode("use", { href: `#${id}`, x, y, width: size, height: size, class: "piece" });
    use.style.color = colour;
    layer.append(use);
  } else if (fallback) {
    const glyph = svgNode("text", {
      x: x + size / 2, y: y + size * 0.8, "text-anchor": "middle", "font-size": size,
      fill: colour, stroke: "#160f08", "stroke-width": 1
    });
    glyph.textContent = fallback;
    layer.append(glyph);
  }
}

/* Three layers so a neighbour's pieces never cover an army number:
 * selection rings, then pieces and markers, then the number badges. */
function renderBadges(marks) {
  const group = state.badges;
  const game = state.game;
  const meta = state.mapMeta;
  if (!group || !game || !meta) return;
  const rings = svgNode("g", { class: "layer-rings" });
  const pieces = svgNode("g", { class: "layer-pieces" });
  const numbers = svgNode("g", { class: "layer-numbers" });
  group.replaceChildren(rings, pieces, numbers);
  const sprites = (faction) => (((meta.factions || [])[faction] || {}).sprites) || {};
  const printed = game.printed || {};
  const frozenSet = new Set((printed.frozen || []).map(([t]) => t));
  for (const [b] of printed.blockedBridges || []) {
    const bridge = (meta.bridges || [])[b];
    if (!bridge) continue;
    const [ax, ay] = meta.territories[bridge.a].label;
    const [bx, by] = meta.territories[bridge.b].label;
    const block = svgNode("text", { x: (ax + bx) / 2, y: (ay + by) / 2 + 6, "text-anchor": "middle",
      "font-size": 18, "font-weight": 700, fill: "#ff5a3d", stroke: "#160f08", "stroke-width": 1.2 });
    block.textContent = "⛔";
    rings.append(block);
  }
  const count = Math.min(game.territories.length, meta.territories.length);
  for (let i = 0; i < count; i++) {
    const territory = game.territories[i];
    const def = meta.territories[i];
    const label = def.label;
    if (!label) continue;
    const [x, y] = label;
    const ring = marks && (i === marks.source ? "source" : i === marks.target ? "target"
      : marks.selectable.has(i) ? "selectable" : "");
    if (ring) {
      /* The army counter is the most visible part of a territory, so it
       * carries the "you can click this" cue too.  A dark halo under a light
       * ring keeps it readable on parchment and on every faction colour. */
      for (const part of ["ring-halo", `ring ${ring}`]) {
        rings.append(svgNode("circle", { cx: x, cy: y, r: 19, class: part }));
      }
    }
    /* Strongholds and sites are shown even before a territory is claimed. */
    const markers = [];
    if (def.stronghold) markers.push(["stronghold", "#d8c9a0", "⌂"]);
    if (def.site) markers.push(["site", "#ffe08a", "✦"]);
    markers.forEach(([kind, colour, glyph], index) => {
      drawPiece(pieces, MARKER_SPRITES[kind], x - 31, y - 29 + index * 17, 16, colour, glyph);
    });
    if (!territory.neutral && territory.owner < 0) continue;
    const colour = territory.neutral ? "#8a8a8a" : factionHex(territory.owner);
    const set = sprites(territory.neutral ? 0 : playerFaction(territory.owner));
    const { tokens, more } = armyTokens(territory.armies);
    /* Tokens overlap like a small stack so dense areas stay readable. */
    tokens.forEach((size, index) => {
      drawPiece(pieces, set[`unit${size}`], x + 13 + index * 6, y - 8, 16, colour, null);
    });
    if (more && state.pieceSymbols && set.unit5 && state.pieceSymbols[set.unit5]) {
      const plus = svgNode("text", {
        x: x + 13 + tokens.length * 6 + 12, y: y + 4, "font-size": 12, "font-weight": 700,
        fill: "#fff7e8", stroke: "#160f08", "stroke-width": 0.8
      });
      plus.textContent = "+";
      pieces.append(plus);
    }
    if (territory.leader) {
      const seat = territory.leader - 1;
      drawPiece(pieces, sprites(playerFaction(seat)).leader, x + 4, y - 32, 19,
        factionHex(seat), "★");
    }
    if (frozenSet.has(i)) {
      const flake = svgNode("text", { x: x - 6, y: y + 30, "font-size": 15, fill: "#bfe6ff",
        stroke: "#0b2233", "stroke-width": 1, class: "frozen-mark" });
      flake.textContent = "❄";
      pieces.append(flake);
    }
    numbers.append(svgNode("circle", {
      cx: x, cy: y, r: 13, fill: colour, "fill-opacity": 0.94, stroke: "#160f08", "stroke-width": 2
    }));
    const text = svgNode("text", {
      x, y: y + 4.5, "text-anchor": "middle", "font-size": 13, "font-weight": 700,
      fill: "#fff7e8", "font-family": "system-ui, sans-serif"
    });
    text.textContent = String(territory.armies);
    numbers.append(text);
  }
}

function updateTitle() {
  const game = state.game;
  if (state.viewMode === "game" && game && game.legal && game.status === "running" &&
      game.you >= 0 && game.current === game.you) {
    document.title = "(!) Your turn — FRISK";
  } else {
    document.title = "FRISK";
  }
}

async function act(type, fields) {
  const result = await post("/api/game/act", { g: state.gameId, type, ...fields });
  if (!result.ok) { toast(errorMessage(result), true); return false; }
  state.selectedFrom = null;
  state.selectedTo = null;
  state.pendingPlace = null;
  state.pendingLeader = null;
  state.bringLeader = false;
  await loadGame(true);
  return true;
}

const chatViews = {};

function chatCanPost(view) {
  if (!state.me || !view || !view.room) return false;
  if (view.room === "lobby") return true;
  const game = state.game;
  if (!game || view.room !== `g${game.id}`) return false;
  return game.you >= 0;
}

function chatTime(seconds) {
  if (!Number.isFinite(seconds) || seconds <= 0) return "";
  try {
    return new Date(seconds * 1000).toLocaleTimeString([], {
      hour: "2-digit", minute: "2-digit"
    });
  } catch (error) {
    return "";
  }
}

function chatDuration(milliseconds) {
  const total = Math.max(0, Math.round(Number(milliseconds) / 1000));
  if (!total) return "clip";
  return `${Math.floor(total / 60)}:${String(total % 60).padStart(2, "0")}`;
}

function updateChatControls(view) {
  if (!view) return;
  const canPost = chatCanPost(view);
  if (view.text) view.text.disabled = !canPost;
  if (view.sendButton) view.sendButton.disabled = !canPost;
  if (view.taunts) {
    for (const item of view.taunts.querySelectorAll("button")) {
      item.disabled = !canPost;
    }
  }
  if (view.voice) view.voice.setEnabled(canPost);
  if (!view.status) return;
  if (!state.me) {
    view.status.textContent = "Sign in to chat.";
  } else if (!view.room) {
    view.status.textContent = "Loading chat…";
  } else if (!canPost) {
    view.status.textContent = "Spectators can read this room.";
  } else if (!view.loaded) {
    view.status.textContent = "Loading chat…";
  } else {
    view.status.textContent = "";
  }
}

function chatView(prefix) {
  if (chatViews[prefix]) return chatViews[prefix];
  const view = {
    prefix,
    room: null,
    active: false,
    loading: false,
    loaded: false,
    lastId: 0,
    seen: new Set(),
    at: 0,
    audio: null,
    tauntAudio: null,
    log: $(`${prefix}-chat-log`),
    status: $(`${prefix}-chat-status`),
    form: $(`${prefix}-chat-form`),
    text: $(`${prefix}-chat-text`),
    taunts: $(`${prefix}-chat-taunts`),
    autoplay: $(`${prefix}-chat-autoplay`),
    voiceRoot: $(`${prefix}-chat-voice`),
    voice: null,
    sendButton: null
  };
  if (!view.log || !view.form) return null;
  view.sendButton = view.form.querySelector("button[type=submit]");
  CHAT_TAUNTS.forEach((label, index) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "secondary tiny taunt-button";
    button.textContent = `♪ ${label}`;
    button.title = "Send this taunt to the room";
    button.addEventListener("click", () => sendChatTaunt(view, index));
    view.taunts.append(button);
  });
  view.autoplay.checked = chatAutoplay();
  view.autoplay.addEventListener("change", () => {
    setChatAutoplay(view.autoplay.checked);
  });
  view.form.addEventListener("submit", (event) => {
    event.preventDefault();
    sendChatText(view);
  });
  if (window.FriskVoice && view.voiceRoot) {
    view.voice = window.FriskVoice.mount(view.voiceRoot, {
      onClip: (blob, name, duration) => sendChatClip(view, blob, name, duration),
      onError: (message) => toast(message, true)
    });
  }
  chatViews[prefix] = view;
  updateChatControls(view);
  return view;
}

function resetChat(view) {
  if (!view) return;
  view.loaded = false;
  view.lastId = 0;
  view.seen.clear();
  view.at = 0;
  if (view.audio) view.audio.pause();
  if (view.tauntAudio) view.tauntAudio.pause();
  view.audio = null;
  view.tauntAudio = null;
  view.log.replaceChildren();
  if (view.voice) view.voice.reset();
}

function activateChat(view, room) {
  if (!view) return;
  if (view.room !== room) {
    view.room = room;
    resetChat(view);
  }
  view.active = true;
  updateChatControls(view);
  pollChat(view, true);
}

function deactivateChat(view) {
  if (!view) return;
  view.active = false;
  if (view.audio) view.audio.pause();
  if (view.tauntAudio) view.tauntAudio.pause();
}

function renderChatMessage(view, message, fresh) {
  const kind = String(message.kind || "text");
  const entry = document.createElement("div");
  entry.className = `chat-entry ${kind}`;
  const meta = document.createElement("div");
  meta.className = "chat-meta";
  const user = document.createElement("span");
  user.className = "chat-user";
  user.textContent = message.user || "someone";
  const time = document.createElement("span");
  time.className = "chat-time";
  time.textContent = chatTime(Number(message.t));
  if (kind === "system") {
    entry.textContent = message.text || "";
  } else {
    meta.append(user, time);
    entry.append(meta);
    if (kind === "text") {
      const text = document.createElement("div");
      text.className = "chat-text";
      text.textContent = message.text || "";
      entry.append(text);
    } else if (kind === "clip") {
      const row = document.createElement("div");
      row.className = "chat-clip";
      const play = document.createElement("button");
      play.type = "button";
      play.className = "secondary tiny chat-play";
      play.textContent = `▶ ${chatDuration(message.dur)}`;
      play.addEventListener("click", () => playClip(view, message));
      const size = document.createElement("span");
      size.className = "chat-size";
      size.textContent = message.size
        ? `${Math.max(1, Math.round(Number(message.size) / 1024))} KB` : "";
      row.append(play, size);
      entry.append(row);
      if (fresh && view.autoplay && view.autoplay.checked &&
          message.user !== state.me && !document.hidden) {
        setTimeout(() => playClip(view, message), 80);
      }
    } else if (kind === "taunt") {
      const row = document.createElement("div");
      row.className = "chat-clip";
      const play = document.createElement("button");
      play.type = "button";
      play.className = "secondary tiny chat-play";
      const index = Number(message.index);
      const label = CHAT_TAUNTS[index] || "Taunt";
      play.textContent = `♪ ${label}`;
      play.addEventListener("click", () => playTaunt(view, index));
      row.append(play);
      entry.append(row);
      if (fresh && message.user !== state.me && !document.hidden) {
        setTimeout(() => playTaunt(view, index), 80);
      }
    }
  }
  const nearBottom = view.log.scrollHeight - view.log.scrollTop -
    view.log.clientHeight < 60;
  view.log.append(entry);
  if (nearBottom) view.log.scrollTop = view.log.scrollHeight;
}

function playClip(view, message) {
  if (!message.file) return;
  if (view.audio) view.audio.pause();
  const audio = new Audio(`/api/clip?f=${encodeURIComponent(message.file)}`);
  view.audio = audio;
  audio.addEventListener("error", () => {
    toast("The clip could not be played.", true);
  });
  const playing = audio.play();
  if (playing && playing.catch) playing.catch(() => { /* autoplay blocked */ });
}

function playTaunt(view, index) {
  if (!Number.isInteger(index) || index < 0 || index >= CHAT_TAUNTS.length) {
    return;
  }
  if (view.tauntAudio) view.tauntAudio.pause();
  const audio = new Audio(`/sounds/taunts/taunt${index + 1}.mp3`);
  view.tauntAudio = audio;
  const playing = audio.play();
  if (playing && playing.catch) playing.catch(() => { /* autoplay blocked */ });
}

async function pollChat(view, force) {
  if (!view || !view.active || !state.me || !view.room || view.loading) return;
  if (!force && Date.now() - view.at < 1200) return;
  view.loading = true;
  try {
    let path = `/api/chat?room=${encodeURIComponent(view.room)}&limit=100`;
    if (view.loaded) path += `&after=${view.lastId}`;
    const result = await get(path);
    if (!view.active) return;
    if (!result || result.err) {
      if (result && result.err === "login") {
        await refreshMe(true);
      } else if (result && result.err === "network") {
        noteOffline();
      } else if (result && result.err === "notfound") {
        view.status.textContent = "This chat room is no longer available.";
      } else if (result && result.err) {
        toast(errorMessage(result), true);
      }
      return;
    }
    noteOnline();
    const wasLoaded = view.loaded;
    const messages = Array.isArray(result.messages) ? result.messages : [];
    for (const message of messages) {
      if (!message) continue;
      const id = Number(message.id);
      if (!Number.isFinite(id) || view.seen.has(id)) continue;
      view.seen.add(id);
      renderChatMessage(view, message, wasLoaded);
      if (wasLoaded && view === chatViews.game && message.kind !== "system" &&
          message.user !== (state.me && state.me.user) && !tabVisible("chat")) {
        tabs.chatUnread++;
      }
    }
    if (Number.isFinite(Number(result.last))) {
      view.lastId = Math.max(view.lastId, Number(result.last));
    }
    view.loaded = true;
    view.at = Date.now();
    updateChatControls(view);
    if (view === chatViews.game) updateTabBadges();
  } finally {
    view.loading = false;
  }
}

async function sendChatText(view) {
  if (!chatCanPost(view)) return;
  const text = view.text.value.trim();
  if (!text) return;
  const result = await post("/api/chat/say", {
    room: view.room, text
  });
  if (!result.ok) {
    toast(errorMessage(result), true);
    return;
  }
  view.text.value = "";
  view.at = 0;
  pollChat(view, true);
}

async function sendChatTaunt(view, index) {
  if (!chatCanPost(view)) return;
  const result = await post("/api/chat/say", {
    room: view.room, taunt: index
  });
  if (!result.ok) {
    toast(errorMessage(result), true);
    return;
  }
  playTaunt(view, index);
  view.at = 0;
  pollChat(view, true);
}

async function sendChatClip(view, blob, name, durationMs) {
  if (!chatCanPost(view)) return false;
  const result = await postForm("/api/chat/clip", {
    room: view.room,
    dur: Math.max(0, Math.round(durationMs || 0)),
    clip: blob
  });
  if (!result.ok) {
    toast(errorMessage(result), true);
    return false;
  }
  view.at = 0;
  pollChat(view, true);
  return true;
}

/* ---------------------------------------------------------------- */
/* Keyboard shortcuts                                                */
/* ---------------------------------------------------------------- */

function typingTarget(target) {
  if (!target) return false;
  const tag = target.tagName;
  /* A focused slider or checkbox still lets Enter confirm the action. */
  if (tag === "INPUT" && (target.type === "range" || target.type === "checkbox")) return false;
  return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" ||
    target.isContentEditable;
}

function interactiveTarget(target) {
  if (typingTarget(target)) return true;
  return target && ["BUTTON", "A", "SUMMARY"].includes(target.tagName);
}

document.addEventListener("keydown", (event) => {
  if (state.viewMode !== "game" || event.altKey || event.ctrlKey || event.metaKey) {
    return;
  }
  if (event.key === "Escape") {
    if (typingTarget(event.target)) return;
    event.preventDefault();
    if (state.replayActive) {
      finishReplay();
      return;
    }
    if (tabs.open && dockLayout()) {
      tabs.open = false;
      applyTabs();
      return;
    }
    state.selectedFrom = null;
    state.selectedTo = null;
    state.pendingPlace = null;
    state.pendingLeader = null;
    state.cardPlay = null;
    state.bringLeader = false;
    renderGame();
    return;
  }
  if (typingTarget(event.target) || interactiveTarget(event.target)) return;
  if (event.key === "Enter") {
    const confirm = document.querySelector("#game-actions button[data-confirm]");
    if (confirm && !confirm.disabled) {
      event.preventDefault();
      confirm.click();
    }
    return;
  }
  if (["1", "2", "3"].includes(event.key) && !event.repeat) {
    const game = state.game;
    const pair = game && game.legal
      ? attackPair(state.selectedFrom, state.selectedTo) : null;
    if (!pair || Number(event.key) > pair.maxDice) return;
    const button = [...document.querySelectorAll("#game-actions button")]
      .find((item) => item.textContent.startsWith(`${event.key} `));
    if (button && !button.disabled) {
      event.preventDefault();
      button.click();
    }
  }
});

/* ---------------------------------------------------------------- */
/* Replay, seen markers and notifications                            */
/* ---------------------------------------------------------------- */

async function markSeen() {
  const game = state.game;
  if (!game || !state.gameId || game.you < 0) return;
  const player = game.players[game.you];
  if (!player || player.lastSeen >= game.version) return;
  const version = game.version;
  const result = await post("/api/game/act", { g: state.gameId, type: "seen" });
  if (result.ok && state.game && state.game.players[state.game.you]) {
    state.game.players[state.game.you].lastSeen = version;
  }
}

function replaySpatial(event) {
  return ["attack", "conquest", "move_in", "fortify",
          "place", "place_leader", "claim"].includes(event.kind);
}

function scheduleReplayStep() {
  clearTimeout(state.replayTimer);
  const delay = Math.max(90, Math.round(400 / state.replaySpeed));
  state.replayTimer = setTimeout(replayStep, delay);
}

function replayStep() {
  if (!state.replayActive) return;
  if (state.replayIndex >= state.replayEvents.length) {
    finishReplay();
    return;
  }
  const event = state.replayEvents[state.replayIndex++];
  if (replaySpatial(event)) {
    const { from, to } = eventTerritories(event);
    state.replayFrom = from;
    state.replayTo = to;
  } else {
    state.replayFrom = null;
    state.replayTo = null;
  }
  let text = `${state.replayIndex}/${state.replayEvents.length} · ` +
    (event.text || event.kind);
  if (event.battles && event.battles.length) {
    const battle = event.battles[event.battles.length - 1];
    text += ` — losses ${battle.attackerLosses}/${battle.defenderLosses}`;
  }
  $("replay-label").textContent = text;
  renderMap();
  scheduleReplayStep();
}

function cancelReplay(mark) {
  clearTimeout(state.replayTimer);
  state.replayTimer = null;
  state.replayActive = false;
  state.replayFrom = null;
  state.replayTo = null;
  if (!$("replay-bar").classList.contains("hidden")) {
    $("replay-bar").classList.add("hidden");
    refitIfFitted();
  }
  if (mark) markSeen();
}

function finishReplay() {
  cancelReplay(true);
  if (state.viewMode === "game") renderGame();
}

async function maybeStartReplay(gameData) {
  if (!gameData || gameData.you < 0 || gameData.status === "open") return;
  const player = gameData.players[gameData.you];
  if (!player) return;
  const lastSeen = player.lastSeen || 0;
  if (gameData.version <= lastSeen) return;
  const result = await get(`/api/game/log?g=${gameData.id}&after=${lastSeen}&limit=100`);
  if (!result.ok || state.viewMode !== "game" || state.gameId !== gameData.id) return;
  const events = result.events || [];
  if (events.length === 0) { markSeen(); return; }
  state.replayActive = true;
  state.replayEvents = events;
  state.replayIndex = 0;
  state.replaySpeed = 1;
  state.replayFrom = null;
  state.replayTo = null;
  $("replay-speed").textContent = "1×";
  $("replay-bar").classList.remove("hidden");
  refitIfFitted();
  hideDice();
  renderEndBanner();
  renderActions();
  replayStep();
}

$("replay-skip").addEventListener("click", finishReplay);
$("replay-speed").addEventListener("click", () => {
  state.replaySpeed = state.replaySpeed === 1 ? 2 : state.replaySpeed === 2 ? 4 : 1;
  $("replay-speed").textContent = `${state.replaySpeed}×`;
  if (state.replayActive) scheduleReplayStep();
});

function alertsStored() {
  try { return localStorage.getItem("friskAlerts") === "on"; }
  catch (error) { return false; }
}

function alertsReady() {
  return typeof Notification !== "undefined" &&
    Notification.permission === "granted" && alertsStored();
}

function updateAlertsUI() {
  const button = $("enable-alerts");
  if (typeof Notification === "undefined") {
    button.disabled = true;
    button.textContent = "Turn alerts unavailable";
    $("alerts-msg").textContent = "This browser cannot show desktop notifications.";
    return;
  }
  const granted = Notification.permission === "granted";
  const on = granted && alertsStored();
  button.textContent = on ? "Disable turn alerts" : "Enable turn alerts";
  if (on) {
    $("alerts-msg").textContent = "Turn alerts are on for hidden tabs.";
  } else if (granted) {
    $("alerts-msg").textContent = "Turn alerts are off.";
  } else {
    $("alerts-msg").textContent = `Browser notification permission: ${Notification.permission}.`;
  }
}

$("enable-alerts").addEventListener("click", async () => {
  if (typeof Notification === "undefined") { updateAlertsUI(); return; }
  if (alertsStored() && Notification.permission === "granted") {
    try { localStorage.setItem("friskAlerts", "off"); } catch (error) { /* unavailable */ }
    updateAlertsUI();
    return;
  }
  try {
    const permission = await Notification.requestPermission();
    try { localStorage.setItem("friskAlerts", permission === "granted" ? "on" : "off"); }
    catch (error) { /* unavailable */ }
  } catch (error) {
    say("alerts-msg", "Could not ask for notification permission.", false);
  }
  updateAlertsUI();
});

function turnNotification(title, body, tag) {
  try {
    new Notification(title, { body, tag, renotify: false });
  } catch (error) { /* notifications can fail on insecure origins */ }
}

function isMyTurn(game) {
  return !!(game && game.status === "running" && game.you >= 0 && game.current === game.you);
}

function maybeNotifyTurn() {
  const game = state.game;
  if (!game || state.viewMode !== "game" || !document.hidden) return;
  if (!alertsReady() || game.status !== "running" || game.you < 0) return;
  if (game.current !== game.you || state.notifiedTurn === game.turn) return;
  state.notifiedTurn = game.turn;
  turnNotification("Your turn — FRISK",
    `${game.name}: ${phaseLabel(game.phase)}`, `frisk-turn-${game.id}`);
}

function maybeNotifyLobby() {
  if (!document.hidden || !alertsReady() || state.viewMode !== "lobby" || !state.lobby) return;
  const game = (state.lobby.yourGames || []).find((entry) => entry.yourTurn);
  if (!game) return;
  const key = `${game.id}:${game.version}`;
  if (state.notifiedGameKey === key) return;
  state.notifiedGameKey = key;
  turnNotification("Your turn — FRISK",
    `${game.name}: waiting for you`, `frisk-turn-${game.id}`);
}

/* ---------------------------------------------------------------- */
/* Polling                                                           */
/* ---------------------------------------------------------------- */

setInterval(async () => {
  if (!state.me) return;
  const visible = document.visibilityState === "visible";
  if (state.viewMode === "game" && state.gameId) {
    const interval = visible ? 1000 : 20000;
    if (Date.now() - state.gameAt >= interval) await loadGame(false);
  } else if (state.viewMode === "lobby") {
    const interval = visible ? 5000 : 30000;
    if (Date.now() - state.lobbyAt >= interval) await refreshLobby(false);
  }
  const chat = state.viewMode === "game" ? chatViews.game
    : state.viewMode === "lobby" ? chatViews.lobby : null;
  if (chat && chat.active) {
    const interval = visible
      ? (state.viewMode === "game" ? 2000 : 3000) : 20000;
    if (Date.now() - chat.at >= interval) await pollChat(chat, false);
  }
}, 1000);

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    if (state.viewMode === "game") loadGame(false);
    else if (state.viewMode === "lobby") refreshLobby(false);
    const chat = state.viewMode === "game" ? chatViews.game
      : state.viewMode === "lobby" ? chatViews.lobby : null;
    if (chat && chat.active) pollChat(chat, true);
  } else if (state.viewMode === "game" && !state.replayActive) {
    markSeen();
  }
});

/* ---------------------------------------------------------------- */
/* Boot                                                              */
/* ---------------------------------------------------------------- */

updateAlertsUI();
refreshMe().then((loggedIn) => {
  if (loggedIn) {
    loadMaps();
    refreshLobby(true);
    loadInvites();
  }
});
})();
