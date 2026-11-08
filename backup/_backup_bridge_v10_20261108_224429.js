import { Zalo } from "zca-js";
import fs from "fs";
import path from "path";
import os from "os";
import readline from "readline";
import { getCookies } from "@steipete/sweet-cookie";
import QRCode from "qrcode";
import { spawn } from "child_process";
import crypto from "crypto";

function log(...args) { process.stdout.write("LOG:" + args.join(" ") + "\n"); }
function emit(obj)     { process.stdout.write("EVT:" + JSON.stringify(obj) + "\n"); }
function emitResponse(id, ok, result) {
  process.stdout.write("RES:" + JSON.stringify({ id, ok, result }) + "\n");
}

// ============================================================
// imageMetadataGetter Ã¢â‚¬â€ Ã„â€˜Ã¡Â»Âc header PNG
// ============================================================
async function imageMetadataGetter(filePath) {
  try {
    const data = fs.readFileSync(filePath);
    let width = 0, height = 0;

    if (data.length > 24 &&
        data[0] === 0x89 && data[1] === 0x50 &&
        data[2] === 0x4E && data[3] === 0x47) {
      width  = data.readUInt32BE(16);
      height = data.readUInt32BE(20);
    }

    return { width, height, size: data.length };
  } catch (e) {
    log("imageMetadataGetter ERROR: " + e.message);
    return { width: 720, height: 400, size: 0 };
  }
}

async function readChromeCookies() {
  try {
    const result = await getCookies({
      url: "https://chat.zalo.me",
      names: ["zpsid", "zpw_sek", "__zi", "app.event.zalo.me", "_zlang"],
    });
    if (result.warnings?.length) log("Ã¢Å¡Â Ã¯Â¸Â sweet-cookie:", result.warnings.join(", "));
    return result.cookies || [];
  } catch (e) {
    log("Ã¢ÂÅ’ LÃ¡Â»â€”i Ã„â€˜Ã¡Â»Âc cookie Chrome:", e.message);
    return [];
  }
}

function findChromeProfileDir() {
  const home = os.homedir();
  const candidates = [
    path.join(home, "AppData", "Local", "Google", "Chrome", "User Data", "Default"),
    path.join(home, "AppData", "Local", "Google", "Chrome", "User Data", "Profile 1"),
    path.join(home, "AppData", "Local", "Microsoft", "Edge", "User Data", "Default"),
    path.join(home, "AppData", "Local", "CocCoc", "Browser", "User Data", "Default"),
  ];
  for (const c of candidates) {
    if (fs.existsSync(path.join(c, "Network", "Cookies")) ||
        fs.existsSync(path.join(c, "Cookies"))) return c;
  }
  return "";
}

function readIMEIFromChrome(profileDir) {
  const ldbDir = path.join(profileDir, "Local Storage", "leveldb");
  if (!fs.existsSync(ldbDir)) return "";
  const uuidRe = /[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}-[0-9a-f]{32}/;
  try {
    const files = fs.readdirSync(ldbDir).filter(f => f.endsWith(".log") || f.endsWith(".ldb"));
    for (const file of files) {
      const content = fs.readFileSync(path.join(ldbDir, file)).toString("binary");
      for (const key of ["sh_z_uuid", "z_uuid"]) {
        let idx = content.indexOf(key);
        while (idx !== -1) {
          const m = content.substring(idx, idx + 300).match(uuidRe);
          if (m) return m[0];
          idx = content.indexOf(key, idx + 1);
        }
      }
    }
  } catch (e) { log("Ã¢Å¡Â Ã¯Â¸Â LÃ¡Â»â€”i Ã„â€˜Ã¡Â»Âc IMEI:", e.message); }
  return "";
}

function getChromeUA() {
  return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
         "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
}

async function getCredentials() {
  // Doc config.json - uu tien cao nhat, don gian hoa hoan toan
  const fs = await import("fs");
  if (!fs.existsSync("config.json")) {
    log("[Creds] config.json KHONG TON TAI");
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  let raw;
  try { raw = fs.readFileSync("config.json", "utf8"); }
  catch (e) {
    log("[Creds] Khong doc duoc file: " + e.message);
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  log("[Creds] config.json size=" + raw.length + " bytes");

  let cfg;
  try { cfg = JSON.parse(raw); }
  catch (e) {
    log("[Creds] JSON.parse FAIL: " + e.message);
    log("[Creds] First 100 chars: " + raw.substring(0, Math.min(100, raw.length)));
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  const ck = cfg && cfg.cookie;
  if (!ck) {
    log("[Creds] cfg.cookie undefined");
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  let arr = [];
  if (Array.isArray(ck)) {
    arr = ck;
    log("[Creds] cookie is ARRAY, length=" + ck.length);
  } else if (ck && Array.isArray(ck.cookies)) {
    arr = ck.cookies;
    log("[Creds] cookie is OBJECT.cookies, length=" + ck.cookies.length);
  } else {
    log("[Creds] cookie format KHONG HO TRO: type=" + typeof ck + ", keys=" + Object.keys(ck || {}).join(","));
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  arr = arr.filter(c => c && (c.name || c.key) && c.value !== undefined)
           .map(c => {
             const out = Object.assign({}, c);
             out.name = c.name || c.key;
             delete out.key;
             out.value = String(c.value);
             if (!out.domain) out.domain = ".zalo.me";
             if (!out.path) out.path = "/";
             return out;
           });

  log("[Creds] after filter/map: " + arr.length + " cookies");
  if (arr.length > 0) {
    log("[Creds] first: " + arr[0].name + " | domain=" + arr[0].domain + " | value.len=" + arr[0].value.length);
  }

  return {
    cookie: arr,
    imei: cfg.imei || "",
    user_agent: cfg.user_agent || getChromeUA(),
  };
}


// ============================================================
// QR LOGIN FALLBACK
// ============================================================
function openImage(p) {
  try {
    if (process.platform === "win32") {
      spawn("cmd", ["/c", "start", "", p], { detached: true, stdio: "ignore" }).unref();
    } else if (process.platform === "darwin") {
      spawn("open", [p], { detached: true, stdio: "ignore" }).unref();
    } else {
      spawn("xdg-open", [p], { detached: true, stdio: "ignore" }).unref();
    }
  } catch (e) {
    log("openImage error: " + e.message);
  }
}

function saveCookieToConfig(api, prevCreds) {
  try {
    let cookie = null;
    try { cookie = api.getCookie(); } catch (e) { log("[Save] getCookie threw: " + e.message); }
    if (!cookie) { log("[Save] getCookie null, skip"); return; }

    log("[Save] getCookie ctor=" + (cookie.constructor ? cookie.constructor.name : "?"));

    if (cookie && typeof cookie.serializeSync === "function") {
      log("[Save] goi serializeSync()");
      try { cookie = cookie.serializeSync(); } catch (e) { log("[Save] serializeSync err: " + e.message); }
    }

    let cookieArr = [];
    if (Array.isArray(cookie)) { cookieArr = cookie; }
    else if (cookie && Array.isArray(cookie.cookies)) { cookieArr = cookie.cookies; }
    else {
      log("[Save] unwrap fail: keys=" + Object.keys(cookie || {}).join(","));
      return;
    }

    log("[Save] cookieArr.length=" + cookieArr.length);
    if (cookieArr.length === 0) { log("[Save] mang rong"); return; }

    const normalized = cookieArr.map(c => {
      const out = Object.assign({}, c);
      out.name = c.name || c.key;
      delete out.key;
      out.value = String(c.value);
      if (!out.domain) out.domain = ".zalo.me";
      if (!out.path) out.path = "/";
      return out;
    });

    log("[Save] normalized: " + normalized.length + " cookies, first=" + normalized[0].name);

    // === IMEI fallback 5 lop: api -> prevCreds -> config cu -> generate ===
    let imei = "";
    try {
      if (typeof api.getImei === "function") imei = api.getImei() || "";
      if (!imei && api.imei) imei = api.imei;
      if (!imei && api.context && api.context.imei) imei = api.context.imei;
      if (!imei && typeof api.getContext === "function") {
        try { const c = api.getContext(); if (c && c.imei) imei = c.imei; } catch (e2) {}
      }
    } catch (e) {}
    if (!imei && prevCreds && prevCreds.imei && prevCreds.imei.length > 20) {
      imei = prevCreds.imei;
      log("[Save] IMEI tu prevCreds: " + imei.substring(0, 25) + "...");
    }
    if (!imei) {
      try {
        
        if (fs.existsSync("config.json")) {
          const oldCfg = JSON.parse(fs.readFileSync("config.json", "utf8"));
          if (oldCfg.imei && oldCfg.imei.length > 20) {
            imei = oldCfg.imei;
            log("[Save] IMEI giu tu config cu: " + imei.substring(0, 25) + "...");
          }
        }
      } catch (e3) {}
    }
    if (!imei) {
      try {
        
        const uuid = crypto.randomUUID();
        const suffix = crypto.randomBytes(24).toString("hex");
        imei = uuid + "-" + suffix;
        log("[Save] IMEI generated: " + imei.substring(0, 30) + "...");
      } catch (e4) {
        log("[Save] FATAL: khong the generate imei - " + e4.message);
        return;
      }
    }
    log("[Save] IMEI length=" + imei.length);
    const cfg = {
      cookie: normalized,
      imei: imei,
      user_agent: prevCreds?.user_agent || getChromeUA(),
    };

    const json = JSON.stringify(cfg, null, 2);
    const tmpPath = "config.json.tmp";
    const finalPath = "config.json";

    // ATOMIC: ghi tmp -> rename (tranh corrupt giua chung)
    fs.writeFileSync(tmpPath, json, "utf8");
    const tmpSize = fs.statSync(tmpPath).size;
    log("[Save] wrote tmp: " + tmpSize + " bytes");

    fs.renameSync(tmpPath, finalPath);
    const finalSize = fs.statSync(finalPath).size;
    log("[Save] rename OK -> " + finalPath + " (" + finalSize + " bytes, " + normalized.length + " cookies)");

    // Read-back verify
    try {
      const verifyRaw = fs.readFileSync(finalPath, "utf8");
      const verify = JSON.parse(verifyRaw);
      const verifyLen = Array.isArray(verify.cookie) ? verify.cookie.length : 0;
      log("[Save] read-back verify: " + verifyLen + " cookies");
      if (verifyLen !== normalized.length) {
        log("[Save] WARN: mismatch! saved=" + normalized.length + " readback=" + verifyLen);
      }
    } catch (e) {
      log("[Save] read-back FAIL: " + e.message);
    }
  } catch (e) {
    log("[Save] FATAL: " + e.message);
    log("[Save] stack: " + (e.stack || "").substring(0, 300));
  }
}

async function doQRLoginOnce(prevCreds) {
  const qrPath = path.resolve("cache/qr_login.png");
  try { fs.unlinkSync(qrPath); } catch (e) {}

  const zalo = new Zalo({ selfListen: true, imageMetadataGetter });

  let lastOpenMtime = 0;
  let openCount = 0;
  const watcher = setInterval(() => {
    try {
      if (!fs.existsSync(qrPath)) return;
      const st = fs.statSync(qrPath);
      if (st.size < 500) return;
      if (st.mtimeMs <= lastOpenMtime) return;
      lastOpenMtime = st.mtimeMs;
      openCount++;
      log("QR ready: " + qrPath + " (" + st.size + " bytes, lan " + openCount + ")");
      openImage(qrPath);
    } catch (e) {}
  }, 1000);

  try {
    const opts = {
      userAgent: prevCreds?.user_agent || getChromeUA(),
      language: "vi",
      qrPath: qrPath,
    };
    log("Goi loginQR voi qrPath=" + qrPath);
    const api = await zalo.loginQR(opts);
    clearInterval(watcher);
    if (openCount === 0) {
      try {
        if (fs.existsSync(qrPath)) openImage(qrPath);
      } catch (e) {}
    }
    log("QR login thanh cong");
    return api;
  } catch (e) {
    clearInterval(watcher);
    throw e;
  }
}

async function tryQRLogin(prevCreds, maxRetries = 2) {
  for (let i = 1; i <= maxRetries; i++) {
    log("=== QR login lan " + i + "/" + maxRetries + " ===");
    try {
      return await doQRLoginOnce(prevCreds);
    } catch (e) {
      log("QR login lan " + i + " that bai: " + (e?.message || String(e)));
      if (i < maxRetries) {
        log("Thu lai sau 3s...");
        await new Promise(r => setTimeout(r, 3000));
      }
    }
  }
  throw new Error("Tat ca lan QR login deu that bai");
}

const creds = await getCredentials();
const _credsArr = creds && Array.isArray(creds.cookie) ? creds.cookie : [];
log("So cookie doc duoc: " + _credsArr.length + ", imei=" + (creds && creds.imei ? "YES" : "NO"));

let api = null;

if (_credsArr.length >= 2 && creds && creds.imei) {
  try {
    log("Dang login zca-js (cookie)...");
    const zalo = new Zalo({ selfListen: true, imageMetadataGetter });
    api = await zalo.login({
      cookie: _credsArr,
      imei: creds.imei,
      userAgent: creds.user_agent,
    });
    log("Login thanh cong (cookie)");
  } catch (e) {
    log("Cookie login that bai: " + (e && e.message ? e.message : String(e)));
    api = null;
  }
} else {
  log("Khong du cookie hoac thieu imei, bo qua cookie login");
}

if (!api) {
  log("Chuyen sang QR login...");
  try {
    api = await tryQRLogin(creds || {});
    log("Login thanh cong (QR)");
    saveCookieToConfig(api, creds || {});
  } catch (e2) {
    log("QR login that bai: " + (e2 && e2.message ? e2.message : String(e2)));
    process.exit(1);
  }
}

let ownId = "";
try { ownId = api.getOwnId(); log("Own ID:", ownId); }
catch (e) { log("Ã¢Å¡Â Ã¯Â¸Â KhÃƒÂ´ng lÃ¡ÂºÂ¥y Ã„â€˜Ã†Â°Ã¡Â»Â£c ownId:", e.message); }

emit({ event: "login_ok", own_id: String(ownId) });

// ============================================================
// Cache tin nháº¯n Ä‘á»ƒ anti-undo
// ============================================================
const msgCache = new Map();
const MSG_CACHE_TTL = 30 * 60 * 1000; // 30 phÃºt

function cacheMessage(m) {
  try {
    const d = m?.data || {};
    const msgId     = d.msgId;
    const cliMsgId  = d.cliMsgId;
    const globalId  = d.globalMsgId;
    const content   = d.content;

    if (!content) return;

    const entry = {
      content: String(content),
      uidFrom: String(d.uidFrom || ""),
      ts: Date.now(),
    };

    // LÆ°u theo Táº¤T Cáº¢ cÃ¡c key cÃ³ thá»ƒ match khi undo
    if (msgId)    msgCache.set(String(msgId), entry);
    if (cliMsgId) msgCache.set(String(cliMsgId), entry);
    if (globalId) msgCache.set(String(globalId), entry);
  } catch (e) {}
}

function handleUndoEvent(undo) {
  try {
    const u = undo?.data || {};
    const c = u.content || {};

    // Thu tháº­p Táº¤T Cáº¢ cÃ¡c ID cÃ³ thá»ƒ dÃ¹ng Ä‘á»ƒ lookup
    const ids = {
      msgId:       String(u.msgId       || ""),
      realMsgId:   String(u.realMsgId   || ""),
      cliMsgId:    String(u.cliMsgId    || ""),
      cCliMsgId:   String(c.cliMsgId    || ""),
      cGlobalId:   String(c.globalMsgId || ""),
    };
    const uidFrom  = String(u.uidFrom  || "");
    const threadId = String(undo?.threadId || "");
    const isGroup  = !!undo?.isGroup;
    const isSelf   = !!undo?.isSelf;

    // Lookup theo thá»© tá»± Æ°u tiÃªn
    let cached = null;
    let hitKey = "";
    const tryOrder = ["realMsgId", "msgId", "cliMsgId", "cCliMsgId", "cGlobalId"];
    for (const k of tryOrder) {
      const key = ids[k];
      if (!key) continue;
      const found = msgCache.get(key);
      if (found) { cached = found; hitKey = k + "=" + key; break; }
    }

    log("undo event: msgId=" + ids.msgId
        + " realMsgId=" + ids.realMsgId
        + " cliMsgId=" + ids.cliMsgId
        + " c.cliMsgId=" + ids.cCliMsgId
        + " c.globalMsgId=" + ids.cGlobalId
        + " uidFrom=" + uidFrom
        + " threadId=" + threadId
        + " isGroup=" + isGroup
        + " isSelf=" + isSelf
        + " hit=" + (hitKey || "NONE"));

    const original = cached ? cached.content : "";

    emit({
      event: "undo",
      data: {
        msgId: ids.msgId,
        cliMsgId: ids.cliMsgId,
        uidFrom, threadId,
        isGroup, isSelf,
        originalContent: original,
      }
    });
  } catch (e) {
    log("handleUndoEvent error: " + (e?.message || String(e)));
  }
}

// Cleanup cache má»—i 5 phÃºt
setInterval(() => {
  const now = Date.now();
  for (const [k, v] of msgCache.entries()) {
    if (now - v.ts > MSG_CACHE_TTL) msgCache.delete(k);
  }
}, 5 * 60 * 1000);

let reconnectAttempts = 0;
const MAX_RECONNECT = 10;
let isReconnecting = false;

api.listener.on("message", (message) => {
  cacheMessage(message);
  emit({ event: "message", data: message });
});

api.listener.on("undo", handleUndoEvent);

api.listener.on("connected", () => {
  log("Ã¢Å“â€¦ WebSocket connected");
  reconnectAttempts = 0;
  isReconnecting = false;
});

api.listener.on("error", (err) => {
  log("Ã¢ÂÅ’ Listener error:", err?.message || err);
});

api.listener.on("closed", (code, reason) => {
  log("Ã¢Å¡Â Ã¯Â¸Â WebSocket closed:", code, reason);
  emit({ event: "closed", code, reason });

  if (isReconnecting) return;
  isReconnecting = true;

  if (reconnectAttempts >= MAX_RECONNECT) {
    log("Ã¢ÂÅ’ Ã„ÂÃƒÂ£ thÃ¡Â»Â­ reconnect " + MAX_RECONNECT + " lÃ¡ÂºÂ§n. ThoÃƒÂ¡t.");
    process.exit(1);
  }

  reconnectAttempts++;
  const delay = 8000;
  log(`Ã°Å¸â€â€ž Reconnect lÃ¡ÂºÂ§n ${reconnectAttempts} sau ${delay / 1000}s...`);

  setTimeout(async () => {
    try {
      try { api.listener.stop(); } catch (e) {}

      const fresh = await getCredentials();
      if (!fresh || fresh.cookie.length < 2) {
        log("Ã¢ÂÅ’ KhÃƒÂ´ng cÃƒÂ³ cookie");
        isReconnecting = false;
        return;
      }

      const zalo = new Zalo({ selfListen: true, imageMetadataGetter });
      const newApi = await zalo.login({
        cookie: fresh.cookie,
        imei: fresh.imei,
        userAgent: fresh.user_agent,
      });

      api = newApi;

      api.listener.on("message", m => { cacheMessage(m); emit({ event: "message", data: m }); });
      api.listener.on("undo", handleUndoEvent);
      api.listener.on("connected", () => {
        log("Ã¢Å“â€¦ Reconnect OK");
        reconnectAttempts = 0;
        isReconnecting = false;
      });
      api.listener.on("error", (err) => log("Ã¢ÂÅ’ Listener error:", err?.message || err));
      api.listener.on("closed", (c, r) => {
        log("Ã¢Å¡Â Ã¯Â¸Â WS closed (sau reconnect):", c, r);
        isReconnecting = false;
      });

      api.listener.start();
      log("Ã¢Å“â€¦ Reconnect thÃƒÂ nh cÃƒÂ´ng");
    } catch (e) {
      log("Ã¢ÂÅ’ Reconnect thÃ¡ÂºÂ¥t bÃ¡ÂºÂ¡i:", e?.message || e);
      isReconnecting = false;
    }
  }, delay);
});

api.listener.start();
log("Listener started");

// ============================================================
// XÃ¡Â»Â­ lÃƒÂ½ lÃ¡Â»â€¡nh tÃ¡Â»Â« C++ qua stdin
// ============================================================
const rl = readline.createInterface({ input: process.stdin });

rl.on("line", async (line) => {
  let cmd;
  try { cmd = JSON.parse(line); } catch { return; }
  const id = cmd.id;

  try {
    if (cmd.action === "send_message") {
      const payload = { msg: cmd.text };
      if (Array.isArray(cmd.mentions) && cmd.mentions.length > 0) {
        payload.mentions = cmd.mentions;
      }
      const r = await api.sendMessage(payload, cmd.thread_id, cmd.type);
        log("smwi DEBUG text=" + JSON.stringify(cmd.text).substring(0, 200));
        log("smwi DEBUG mentions=" + JSON.stringify(cmd.mentions));
      emitResponse(id, true, r);
    }
    else if (cmd.action === "send_image") {
      try {
        const absPath = path.resolve(cmd.path);
        log("sendImage: path=" + absPath);

        if (!fs.existsSync(absPath)) {
          log("sendImage: FILE NOT FOUND");
          emitResponse(id, false, "file not found: " + absPath);
          return;
        }

        const buf = fs.readFileSync(absPath);
        log("sendImage: size=" + buf.length + " bytes");

        let width = 720, height = 400;
        try {
          const md = await imageMetadataGetter(absPath);
          width = md.width || 720;
          height = md.height || 400;
        } catch (e) {}

        const attachment = {
          data: buf,
          filename: path.basename(absPath),
          metadata: {
            totalSize: buf.length,
            width: width,
            height: height,
          }
        };

        // ThÃ¡Â»Â­ nhiÃ¡Â»Âu format Ã¢â‚¬â€ zca-js version khÃƒÂ¡c nhau cÃ¡ÂºÂ§n khÃƒÂ¡c nhau
        const attempts = [
          { name: "path-string",      payload: { msg: " ", attachments: [absPath] } },
          { name: "buffer",           payload: { msg: " ", attachments: [buf] } },
          { name: "object",           payload: { msg: " ", attachments: [attachment] } },
          { name: "object-empty-msg", payload: { msg: "",  attachments: [attachment] } },
          { name: "path-multi",       payload: { msg: " ", attachments: [absPath, absPath] } },
        ];

        for (const att of attempts) {
          try {
            log("sendImage: trying format=" + att.name);
            const r = await api.sendMessage(att.payload, cmd.thread_id, cmd.type);
            log("sendImage: OK format=" + att.name);
            emitResponse(id, true, r);
            return;
          } catch (e) {
            log("sendImage format=" + att.name + " failed: " + (e?.message || String(e)));
          }
        }

        // Fallback text
        log("sendImage: ALL FORMATS FAILED");
        try {
          await api.sendMessage(
            { msg: "Ã¢ÂÅ’ KhÃƒÂ´ng gÃ¡Â»Â­i Ã„â€˜Ã†Â°Ã¡Â»Â£c Ã¡ÂºÂ£nh. Xem log bot." },
            cmd.thread_id,
            cmd.type
          );
        } catch (e) {}

      } catch (e) {
        log("sendImage ERROR: " + (e?.message || String(e)));
        log("sendImage stack: " + String(e?.stack || "").substring(0, 500));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "send_message_with_image") {
      try {
        const absPath = path.resolve(cmd.image_path);
        if (!fs.existsSync(absPath)) {
          log("smwi: FILE NOT FOUND");
          emitResponse(id, false, "not found");
          return;
        }
        const buf = fs.readFileSync(absPath);
        let w = 720, h = 400;
        try {
          const md = await imageMetadataGetter(absPath);
          w = md.width || 720; h = md.height || 400;
        } catch (e) {}
        const att = {
          data: buf,
          filename: path.basename(absPath),
          metadata: { totalSize: buf.length, width: w, height: h }
        };
        const payload = { msg: cmd.text || " ", attachments: [att] };
        if (Array.isArray(cmd.mentions) && cmd.mentions.length > 0) {
          payload.mentions = cmd.mentions;
        }
        const r = await api.sendMessage(payload, cmd.thread_id, cmd.type);
        log("smwi OK response=" + JSON.stringify(r).substring(0, 400));
        emitResponse(id, true, r);
      } catch (e) {
        log("smwi ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "send_sticker") {
      const r = await api.sendMessage({ sticker: cmd.sticker_id }, cmd.thread_id, cmd.type);
      emitResponse(id, true, r);
    }
    else if (cmd.action === "add_user_to_group") {
      const r = await api.addUserToGroup(cmd.user_id, cmd.group_id);
      emitResponse(id, true, r);
    }
    else if (cmd.action === "remove_user_from_group") {
      const r = await api.removeUserFromGroup(cmd.user_id, cmd.group_id);
      emitResponse(id, true, r);
    }
    else if (cmd.action === "delete_message") {
      try {
        const payload = {
          data: {
            cliMsgId: String(cmd.cli_msg_id),
            msgId: String(cmd.msg_id),
            uidFrom: String(cmd.uid_from)
          },
          threadId: String(cmd.thread_id),
          type: Number(cmd.type)
        };
        log("delete_message: msgId=" + payload.data.msgId
            + " cliMsgId=" + payload.data.cliMsgId
            + " uidFrom=" + payload.data.uidFrom
            + " threadId=" + payload.threadId
            + " type=" + payload.type);
        const r = await api.deleteMessage(payload, false);
        log("delete_message OK: " + JSON.stringify(r).substring(0, 200));
        emitResponse(id, true, r);
      } catch (e) {
        log("delete_message ERROR: " + (e?.message || String(e)));
        log("delete_message stack: " + String(e?.stack || "").substring(0, 300));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "undo_message") {
      try {
        const r = await api.undo(
          { msgId: cmd.msg_id, cliMsgId: cmd.cli_msg_id },
          cmd.thread_id,
          cmd.type
        );
        emitResponse(id, true, r);
      } catch (e) {
        log("undo ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "get_group_info") {
      const r = await api.getGroupInfo(cmd.group_id);
      emitResponse(id, true, r);
    }
    else if (cmd.action === "get_user_info") {
      try {
        log("getUserInfo: " + cmd.user_id);
        const r = await Promise.race([
          api.getUserInfo(cmd.user_id),
          new Promise((_, rej) => setTimeout(() => rej(new Error("timeout")), 25000))
        ]);
        log("getUserInfo OK");
        emitResponse(id, true, r);
      } catch (e) {
        log("getUserInfo ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "add_reaction") {
      const REACTION_MAP = {
        "HEART":         "/-heart",
        "HAHA":          "/-ha",
        "WOW":           "/-wow",
        "CRY":           "/-cry",
        "ANGRY":         "/-angry",
        "LIKE":          "/-like",
        "KISS":          "/-kiss",
        "TEARS_OF_JOY":  "/-tears_of_joy",
        "SHIT":          "/-shit",
        "ROSE":          "/-rose",
        "BROKEN_HEART":  "/-broken_heart",
        "DISLIKE":       "/-dislike",
        "LOVE":          "/-love",
        "CONFUSED":      "/-confused",
        "WINK":          "/-wink",
        "SMILE":         "/-smile",
        "SAD":           "/-sad",
        "ANGRY_2":       "/-angry_2"
      };

      const key = String(cmd.reaction).toUpperCase();
      const reactionStr = REACTION_MAP[key] || "/-heart";

      log("addReaction: " + cmd.reaction + " Ã¢â€ â€™ " + reactionStr
          + " msgId=" + cmd.msg_id);

      try {
        const r = await api.addReaction(
          reactionStr,
          {
            data: { msgId: cmd.msg_id, cliMsgId: cmd.cli_msg_id },
            threadId: cmd.thread_id,
            type: cmd.type
          }
        );
        log("addReaction result: " + JSON.stringify(r).substring(0, 200));
        emitResponse(id, true, r);
      } catch (e) {
        log("addReaction ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }     else if (cmd.action === "download_image") {
      try {
        const res = await fetch(cmd.url, {
          headers: {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
          }
        });
        if (!res.ok) {
          log("download_image HTTP " + res.status);
          emitResponse(id, false, "HTTP " + res.status);
          return;
        }
        const buf = Buffer.from(await res.arrayBuffer());
        fs.writeFileSync(cmd.path, buf);
        log("download_image OK: " + cmd.path + " (" + buf.length + " bytes)");
        emitResponse(id, true, { ok: true, size: buf.length });
      } catch (e) {
        log("download_image ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "update_group_settings") {
      try {
        const opts = {};
        if (typeof cmd.lockSendMsg === "boolean") opts.lockSendMsg = cmd.lockSendMsg;
        if (typeof cmd.joinAppr   === "boolean") opts.joinAppr    = cmd.joinAppr;

        log("updateGroupSettings: gid=" + cmd.group_id
            + " opts=" + JSON.stringify(opts));
        const r = await api.updateGroupSettings(opts, cmd.group_id);
        log("updateGroupSettings OK: " + JSON.stringify(r).substring(0, 200));
        emitResponse(id, true, { ok: true, result: r });
      } catch (e) {
        log("updateGroupSettings ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "create_qr") {
      try {
        const outPath = cmd.path || `data/qr_${Date.now()}.png`;
        const dir = path.dirname(outPath);
        if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

        await QRCode.toFile(outPath, String(cmd.text || ""), {
          width: 500,
          margin: 2,
          errorCorrectionLevel: "M",
          color: { dark: "#000000FF", light: "#FFFFFFFF" }
        });

        log("create_qr OK: " + outPath);
        emitResponse(id, true, { ok: true, path: outPath });
      } catch (e) {
        log("create_qr ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
        else if (cmd.action === "run_python") {
      try {
        const pyCode = cmd.code || "";
        const pyArgs = cmd.args || [];
        const isAdmin = !!cmd.admin;
        const suspicion = parseInt(cmd.suspicion || 0, 10);

        if (pyCode.length > 2000) {
            return emitResponse(id, false, "Code qua dai (toi da 2000 ky tu)");
        }

        const wrapper = Buffer.from("aW1wb3J0IHN5cywganNvbiwgaW8sIGJ1aWx0aW5zLCB0cmFjZWJhY2ssIGFzdAoKdHJ5OgogICAgIyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0KICAgICMgMS4gVEhJRVQgTEFQIEdJT0kgSEFOIFRBSSBOR1VZRU4gKENQVSAmIFJBTSkKICAgICMgPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09CiAgICBNRU1fTElNSVQgPSAxMDI0ICogMTAyNCAqIDEwMjQKICAgIHRyeToKICAgICAgICBpbXBvcnQgcmVzb3VyY2UKICAgICAgICByZXNvdXJjZS5zZXRybGltaXQocmVzb3VyY2UuUkxJTUlUX0FTLCAoTUVNX0xJTUlULCBNRU1fTElNSVQpKQogICAgZXhjZXB0IEltcG9ydEVycm9yOgogICAgICAgIHBhc3MKICAgIAogICAgdHJ5OgogICAgICAgIGltcG9ydCBjdHlwZXMKICAgICAgICBmcm9tIGN0eXBlcyBpbXBvcnQgd2ludHlwZXMgYXMgX1cKICAgICAgICBfayA9IGN0eXBlcy5XaW5ETEwoImtlcm5lbDMyIiwgdXNlX2xhc3RfZXJyb3I9VHJ1ZSkKICAgICAgICBfay5DcmVhdGVKb2JPYmplY3RXLnJlc3R5cGUgPSBfVy5IQU5ETEUKICAgICAgICBfay5DcmVhdGVKb2JPYmplY3RXLmFyZ3R5cGVzID0gW2N0eXBlcy5jX3ZvaWRfcCwgX1cuTFBDV1NUUl0KICAgICAgICBfay5TZXRJbmZvcm1hdGlvbkpvYk9iamVjdC5yZXN0eXBlID0gX1cuQk9PTAogICAgICAgIF9rLlNldEluZm9ybWF0aW9uSm9iT2JqZWN0LmFyZ3R5cGVzID0gW19XLkhBTkRMRSwgY3R5cGVzLmNfaW50LCBjdHlwZXMuY192b2lkX3AsIF9XLkRXT1JEXQogICAgICAgIF9rLkFzc2lnblByb2Nlc3NUb0pvYk9iamVjdC5yZXN0eXBlID0gX1cuQk9PTAogICAgICAgIF9rLkFzc2lnblByb2Nlc3NUb0pvYk9iamVjdC5hcmd0eXBlcyA9IFtfVy5IQU5ETEUsIF9XLkhBTkRMRV0KICAgICAgICBfay5HZXRDdXJyZW50UHJvY2Vzcy5yZXN0eXBlID0gX1cuSEFORExFCgogICAgICAgIGNsYXNzIF9KQihjdHlwZXMuU3RydWN0dXJlKToKICAgICAgICAgICAgX2ZpZWxkc18gPSBbCiAgICAgICAgICAgICAgICAoIlBlclByb2Nlc3NVc2VyVGltZUxpbWl0IiwgY3R5cGVzLmNfaW50NjQpLAogICAgICAgICAgICAgICAgKCJQZXJKb2JVc2VyVGltZUxpbWl0IiwgY3R5cGVzLmNfaW50NjQpLAogICAgICAgICAgICAgICAgKCJMaW1pdEZsYWdzIiwgX1cuRFdPUkQpLAogICAgICAgICAgICAgICAgKCJNaW5pbXVtV29ya2luZ1NldFNpemUiLCBjdHlwZXMuY19zaXplX3QpLAogICAgICAgICAgICAgICAgKCJNYXhpbXVtV29ya2luZ1NldFNpemUiLCBjdHlwZXMuY19zaXplX3QpLAogICAgICAgICAgICAgICAgKCJBY3RpdmVQcm9jZXNzTGltaXQiLCBfVy5EV09SRCksCiAgICAgICAgICAgICAgICAoIkFmZmluaXR5IiwgY3R5cGVzLmNfc2l6ZV90KSwKICAgICAgICAgICAgICAgICgiUHJpb3JpdHlDbGFzcyIsIF9XLkRXT1JEKSwKICAgICAgICAgICAgICAgICgiU2NoZWR1bGluZ0NsYXNzIiwgX1cuRFdPUkQpLAogICAgICAgICAgICBdCiAgICAgICAgY2xhc3MgX0lPKGN0eXBlcy5TdHJ1Y3R1cmUpOgogICAgICAgICAgICBfZmllbGRzXyA9IFsKICAgICAgICAgICAgICAgICgiUmVhZE9wZXJhdGlvbkNvdW50IiwgY3R5cGVzLmNfdWludDY0KSwKICAgICAgICAgICAgICAgICgiV3JpdGVPcGVyYXRpb25Db3VudCIsIGN0eXBlcy5jX3VpbnQ2NCksCiAgICAgICAgICAgICAgICAoIk90aGVyT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLAogICAgICAgICAgICAgICAgKCJSZWFkVHJhbnNmZXJDb3VudCIsIGN0eXBlcy5jX3VpbnQ2NCksCiAgICAgICAgICAgICAgICAoIldyaXRlVHJhbnNmZXJDb3VudCIsIGN0eXBlcy5jX3VpbnQ2NCksCiAgICAgICAgICAgICAgICAoIk90aGVyVHJhbnNmZXJDb3VudCIsIGN0eXBlcy5jX3VpbnQ2NCksCiAgICAgICAgICAgIF0KICAgICAgICBjbGFzcyBfRVhUKGN0eXBlcy5TdHJ1Y3R1cmUpOgogICAgICAgICAgICBfZmllbGRzXyA9IFsKICAgICAgICAgICAgICAgICgiQmFzaWNMaW1pdEluZm9ybWF0aW9uIiwgX0pCKSwKICAgICAgICAgICAgICAgICgiSW9JbmZvIiwgX0lPKSwKICAgICAgICAgICAgICAgICgiUHJvY2Vzc01lbW9yeUxpbWl0IiwgY3R5cGVzLmNfc2l6ZV90KSwKICAgICAgICAgICAgICAgICgiSm9iTWVtb3J5TGltaXQiLCBjdHlwZXMuY19zaXplX3QpLAogICAgICAgICAgICAgICAgKCJQZWFrUHJvY2Vzc01lbW9yeVVzZWQiLCBjdHlwZXMuY19zaXplX3QpLAogICAgICAgICAgICAgICAgKCJQZWFrSm9iTWVtb3J5VXNlZCIsIGN0eXBlcy5jX3NpemVfdCksCiAgICAgICAgICAgIF0KCiAgICAgICAgX2pvYiA9IF9rLkNyZWF0ZUpvYk9iamVjdFcoTm9uZSwgTm9uZSkKICAgICAgICBpZiBfam9iOgogICAgICAgICAgICBfaW5mbyA9IF9FWFQoKQogICAgICAgICAgICAjIExpbWl0RmxhZ3M6IFBST0NFU1NfVElNRSAoMHgyKSB8IEFDVElWRV9QUk9DRVNTICgweDgpIHwgUFJPQ0VTU19NRU1PUlkgKDB4MTAwKSB8IEpPQl9NRU1PUlkgKDB4MjAwMCkKICAgICAgICAgICAgX2luZm8uQmFzaWNMaW1pdEluZm9ybWF0aW9uLkxpbWl0RmxhZ3MgPSAweDIgfCAweDggfCAweDEwMCB8IDB4MjAwMAogICAgICAgICAgICAjIE7hu5tpIGzhu49uZyB0aOG7nWkgZ2lhbiBDUFUgbMOqbiA1IGdpw6J5ICg1MCwwMDAsMDAwICogMTAwbnMpIMSR4buDIHRyw6FuaCBraWxsIG5o4bqnbSBsw7pjIGto4bufaSDEkeG7mW5nCiAgICAgICAgICAgIF9pbmZvLkJhc2ljTGltaXRJbmZvcm1hdGlvbi5QZXJQcm9jZXNzVXNlclRpbWVMaW1pdCA9IDUgKiAxMDAwMDAwMCAKICAgICAgICAgICAgX2luZm8uQmFzaWNMaW1pdEluZm9ybWF0aW9uLkFjdGl2ZVByb2Nlc3NMaW1pdCA9IDEKICAgICAgICAgICAgX2luZm8uUHJvY2Vzc01lbW9yeUxpbWl0ID0gTUVNX0xJTUlUCiAgICAgICAgICAgIF9rLlNldEluZm9ybWF0aW9uSm9iT2JqZWN0KF9qb2IsIDksIGN0eXBlcy5ieXJlZihfaW5mbyksIGN0eXBlcy5zaXplb2YoX2luZm8pKQogICAgICAgICAgICBfay5Bc3NpZ25Qcm9jZXNzVG9Kb2JPYmplY3QoX2pvYiwgX2suR2V0Q3VycmVudFByb2Nlc3MoKSkKICAgIGV4Y2VwdCBFeGNlcHRpb246CiAgICAgICAgcGFzcwoKICAgIHRyeToKICAgICAgICBmb3IgX20gaW4gbGlzdChzeXMubW9kdWxlcy5rZXlzKCkpOgogICAgICAgICAgICBpZiBfbSBpbiAoImN0eXBlcyIsICJfY3R5cGVzIiwgInJlc291cmNlIikgb3IgX20uc3RhcnRzd2l0aCgiY3R5cGVzLiIpIG9yIF9tLnN0YXJ0c3dpdGgoIl9jdHlwZXMuIik6CiAgICAgICAgICAgICAgICBkZWwgc3lzLm1vZHVsZXNbX21dCiAgICAgICAgZm9yIF9uIGluICgiX2siLCJfVyIsIl9KQiIsIl9JTyIsIl9FWFQiLCJfam9iIiwiX2luZm8iLCJfbSIsIl9uIiwiY3R5cGVzIiwicmVzb3VyY2UiKToKICAgICAgICAgICAgdHJ5OiBkZWwgZ2xvYmFscygpW19uXQogICAgICAgICAgICBleGNlcHQgS2V5RXJyb3I6IHBhc3MKICAgIGV4Y2VwdCBFeGNlcHRpb246CiAgICAgICAgcGFzcwoKICAgICMgPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09CiAgICAjIDIuIERPQyBEVSBMSUVVIFRVIFNURElOCiAgICAjID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQogICAgcGF5bG9hZCA9IGpzb24ubG9hZHMoc3lzLnN0ZGluLmJ1ZmZlci5yZWFkKCkuZGVjb2RlKCJ1dGYtOC1zaWciKSkKICAgIGFyZ3MgICAgICA9IHBheWxvYWQuZ2V0KCJhcmdzIiwgW10pCiAgICBjb2RlICAgICAgPSBwYXlsb2FkLmdldCgiY29kZSIsICIiKQogICAgaXNfYWRtaW4gID0gYm9vbChwYXlsb2FkLmdldCgiYWRtaW4iLCBGYWxzZSkpCiAgICBzdXNwaWNpb24gPSBpbnQocGF5bG9hZC5nZXQoInN1c3BpY2lvbiIsIDApKQogICAgYXJnX2lkeCAgID0gMAoKICAgIGlmIGNvZGUuc3RyaXAoKSA9PSAiX19weV9pbmZvX21hZ2ljXzdmM2E5Yl9fIjoKICAgICAgICBwcmludChqc29uLmR1bXBzKHsib3V0Ijogc3lzLnZlcnNpb24uc3BsaXQoKVswXSwgImVyciI6ICIifSkpCiAgICAgICAgc3lzLmV4aXQoMCkKCiAgICBNQVhfQ09ERV9MRU4gPSAyMDAwCiAgICBpZiBsZW4oY29kZSkgPiBNQVhfQ09ERV9MRU46CiAgICAgICAgcHJpbnQoanNvbi5kdW1wcyh7Im91dCI6ICIiLCAiZXJyIjogIkNvZGUgcXVhIGRhaSAodG9pIGRhICVkIGt5IHR1KSIgJSBNQVhfQ09ERV9MRU59KSkKICAgICAgICBzeXMuZXhpdCgwKQoKICAgIGlmIG5vdCBpc19hZG1pbiBhbmQgc3VzcGljaW9uID49IDcwOgogICAgICAgIHByaW50KGpzb24uZHVtcHMoeyJvdXQiOiAiIiwgImVyciI6ICJDb2RlIGJpIGRhbmggZ2lhIHF1YSBraGEgbmdoaSAoJWQgZGllbSkuIENoaSBhZG1pbiBkdW9jIGNoYXkuIiAlIHN1c3BpY2lvbn0pKQogICAgICAgIHN5cy5leGl0KDApCgogICAgIyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0KICAgICMgMy4gS0lFTSBUUkEgQVNUIChCTEFDS0xJU1QpCiAgICAjID09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PQogICAgQUxMT1dFRF9NT0RVTEVTID0geyJtYXRoIiwicmFuZG9tIiwic3RyaW5nIiwiZGF0ZXRpbWUiLCJqc29uIiwicmUiLCJpdGVydG9vbHMiLCJjb2xsZWN0aW9ucyIsInRpbWUifQogICAgRk9SQklEREVOX05BTUVTID0gewogICAgICAgICJfX2J1aWx0aW5zX18iLCJfX2ltcG9ydF9fIiwiZXZhbCIsImV4ZWMiLCJjb21waWxlIiwib3BlbiIsCiAgICAgICAgImdsb2JhbHMiLCJsb2NhbHMiLCJ2YXJzIiwiZGlyIiwiZ2V0YXR0ciIsInNldGF0dHIiLCJkZWxhdHRyIiwKICAgICAgICAiYnJlYWtwb2ludCIsImV4aXQiLCJxdWl0IiwiaGVscCIsIm1lbW9yeXZpZXciLCJjbGFzc21ldGhvZCIsCiAgICAgICAgInN0YXRpY21ldGhvZCIsInByb3BlcnR5Iiwic3VwZXIiLCJvYmplY3QiLCJ0eXBlIiwKICAgICAgICAiX19sb2FkZXJfXyIsIl9fc3BlY19fIiwiX19wYWNrYWdlX18iLCJfX25hbWVfXyIsIl9fZG9jX18iLAogICAgICAgICJfX2ZpbGVfXyIsIl9fY2FjaGVkX18iLCJfX3BhdGhfXyIsIl9fY2xhc3NfXyIsIl9fZGljdF9fIiwiX19nbG9iYWxzX18iLAogICAgICAgICJmX2dsb2JhbHMiLCAiZl9sb2NhbHMiLCAiZl9idWlsdGlucyIsICJnaV9mcmFtZSIsICJjcl9mcmFtZSIsICJhZ19mcmFtZSIKICAgIH0KICAgIERBTkdFUk9VU19BVFRSUyA9IHsKICAgICAgICAiZm9ybWF0IiwgImZvcm1hdF9tYXAiLCAibXJvIiwgInN1YmNsYXNzZXMiLCAKICAgICAgICAiZ2lfZnJhbWUiLCAiY3JfZnJhbWUiLCAiYWdfZnJhbWUiLCAidGJfZnJhbWUiLCAidGJfbmV4dCIsICJmX2dsb2JhbHMiLCAiZl9sb2NhbHMiLCAiZl9idWlsdGlucyIKICAgIH0KCiAgICBkZWYgX3ZhbGlkYXRlX2FzdChzb3VyY2UpOgogICAgICAgIHRyeToKICAgICAgICAgICAgX3RyZWUgPSBhc3QucGFyc2Uoc291cmNlKQogICAgICAgIGV4Y2VwdCBTeW50YXhFcnJvciBhcyBlOgogICAgICAgICAgICByZXR1cm4gc3RyKGUpCiAgICAgICAgZm9yIF9ub2RlIGluIGFzdC53YWxrKF90cmVlKToKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LkF0dHJpYnV0ZSk6CiAgICAgICAgICAgICAgICBpZiBfbm9kZS5hdHRyLnN0YXJ0c3dpdGgoIl9fIikgb3IgX25vZGUuYXR0ciBpbiBEQU5HRVJPVVNfQVRUUlM6CiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgdHJ1eSBjYXAgdGh1b2MgdGluaC9waHVvbmcgdGh1YyAnJXMnIiAlIF9ub2RlLmF0dHIKICAgICAgICAgICAgCiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX25vZGUsIGFzdC5OYW1lKSBhbmQgX25vZGUuaWQgaW4gRk9SQklEREVOX05BTUVTOgogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgZHVuZyAnJXMnIiAlIF9ub2RlLmlkCiAgICAgICAgICAgIAogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuU3Vic2NyaXB0KToKICAgICAgICAgICAgICAgIF9zbGljZSA9IF9ub2RlLnNsaWNlCiAgICAgICAgICAgICAgICBpZiBoYXNhdHRyKGFzdCwgIkluZGV4IikgYW5kIGlzaW5zdGFuY2UoX3NsaWNlLCBhc3QuSW5kZXgpOiAjIFB5dGhvbiA8IDMuOQogICAgICAgICAgICAgICAgICAgIF9zbGljZSA9IF9zbGljZS52YWx1ZQogICAgICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfc2xpY2UsIGFzdC5Db25zdGFudCkgYW5kIGlzaW5zdGFuY2UoX3NsaWNlLnZhbHVlLCBzdHIpOgogICAgICAgICAgICAgICAgICAgIGlmIF9zbGljZS52YWx1ZS5zdGFydHN3aXRoKCJfXyIpIG9yIF9zbGljZS52YWx1ZSBpbiBEQU5HRVJPVVNfQVRUUlM6CiAgICAgICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBwaGVwIHRydXkgY2FwIGtleSBjaHVvaSBuZ3V5IGhpZW0iCiAgICAgICAgICAgIAogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCAoYXN0LkZ1bmN0aW9uRGVmLCBhc3QuQXN5bmNGdW5jdGlvbkRlZiwgYXN0LkNsYXNzRGVmKSk6CiAgICAgICAgICAgICAgICBpZiBfbm9kZS5uYW1lLnN0YXJ0c3dpdGgoIl9fIik6CiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgZGluaCBuZ2hpYSBoYW0vY2xhc3MgY28gdGVuIGJhdCBkYXUgYmFuZyAnX18nIgoKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LkltcG9ydCk6CiAgICAgICAgICAgICAgICBmb3IgX2EgaW4gX25vZGUubmFtZXM6CiAgICAgICAgICAgICAgICAgICAgaWYgX2EubmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6CiAgICAgICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBwaGVwIGltcG9ydCAnJXMnIiAlIF9hLm5hbWUKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LkltcG9ydEZyb20pOgogICAgICAgICAgICAgICAgaWYgX25vZGUubW9kdWxlIGFuZCBfbm9kZS5tb2R1bGUuc3BsaXQoIi4iKVswXSBub3QgaW4gQUxMT1dFRF9NT0RVTEVTOgogICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBwaGVwIGltcG9ydCAnJXMnIiAlIF9ub2RlLm1vZHVsZQogICAgICAgIHJldHVybiBOb25lCgogICAgX2VyciA9IF92YWxpZGF0ZV9hc3QoY29kZSkKICAgIGlmIF9lcnI6CiAgICAgICAgcHJpbnQoanNvbi5kdW1wcyh7Im91dCI6ICIiLCAiZXJyIjogX2Vycn0pKQogICAgICAgIHN5cy5leGl0KDApCgogICAgIyA9PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0KICAgICMgNC4gVEhJRVQgTEFQIE1PSSBUUlVPTkcgVEhVQyBUSEkKICAgICMgPT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09CiAgICBjb2RlX29iaiA9IGNvbXBpbGUoY29kZSwgIjxweT4iLCAiZXhlYyIpCiAgICBpbXBvcnQgbWF0aCwgcmFuZG9tLCBzdHJpbmcsIGRhdGV0aW1lLCBqc29uIGFzIF9weWpzb24sIHJlLCBpdGVydG9vbHMsIGNvbGxlY3Rpb25zLCB0aW1lCgogICAgZGVmIF9mYWtlX2lucHV0KHByb21wdD0iIik6CiAgICAgICAgZ2xvYmFsIGFyZ19pZHgKICAgICAgICBpZiBhcmdfaWR4IDwgbGVuKGFyZ3MpOgogICAgICAgICAgICB2ID0gYXJnc1thcmdfaWR4XTsgYXJnX2lkeCArPSAxOyByZXR1cm4gdgogICAgICAgIHJhaXNlIEVPRkVycm9yKCJIZXQgZHUgbGlldSBkYXUgdmFvIChpbnB1dCkuIikKCiAgICBkZWYgX29yaWdfaW1wb3J0KG5hbWUsICphLCAqKmt3KToKICAgICAgICBpZiBuYW1lLnNwbGl0KCIuIilbMF0gbm90IGluIEFMTE9XRURfTU9EVUxFUzoKICAgICAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwIGltcG9ydCBtb2R1bGUgJyVzJyIgJSBuYW1lKQogICAgICAgIHJldHVybiBidWlsdGlucy5fX2ltcG9ydF9fKG5hbWUsICphLCAqKmt3KQoKICAgIGRlZiBfc2VjX29wZW4oKmEsICoqa3cpOgogICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCB0aGFvIHRhYyB2b2kgZmlsZSIpCgogICAgZGVmIF9zZWNfZm9ybWF0KCphLCAqKmt3KToKICAgICAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBLaG9uZyBkdW9jIHBoZXAgc3UgZHVuZyBwaHVvbmcgdGh1YyAuZm9ybWF0KCkiKQoKICAgIGNsYXNzIFNlY3VyaXR5RXJyb3IoRXhjZXB0aW9uKToKICAgICAgICBwYXNzCgogICAgX1JFTU9WRSA9ICgib3BlbiIsImV4ZWMiLCJldmFsIiwiY29tcGlsZSIsIl9faW1wb3J0X18iLAogICAgICAgICAgICAgICAiZ2V0YXR0ciIsInNldGF0dHIiLCJkZWxhdHRyIiwidmFycyIsImdsb2JhbHMiLCJsb2NhbHMiLCJkaXIiLAogICAgICAgICAgICAgICAiYnJlYWtwb2ludCIsIm1lbW9yeXZpZXciLCJvYmplY3QiLCJ0eXBlIikKICAgIHNhZmVfYnVpbHRpbnMgPSB7azogdiBmb3IgaywgdiBpbiBidWlsdGlucy5fX2RpY3RfXy5pdGVtcygpIGlmIGsgbm90IGluIF9SRU1PVkV9CiAgICBzYWZlX2J1aWx0aW5zLnVwZGF0ZSh7CiAgICAgICAgInByaW50IjogcHJpbnQsICJpbnB1dCI6IF9mYWtlX2lucHV0LCAiX19pbXBvcnRfXyI6IF9vcmlnX2ltcG9ydCwKICAgICAgICAibWF0aCI6IG1hdGgsICJyYW5kb20iOiByYW5kb20sICJzdHJpbmciOiBzdHJpbmcsICJkYXRldGltZSI6IGRhdGV0aW1lLAogICAgICAgICJqc29uIjogX3B5anNvbiwgInJlIjogcmUsICJpdGVydG9vbHMiOiBpdGVydG9vbHMsCiAgICAgICAgImNvbGxlY3Rpb25zIjogY29sbGVjdGlvbnMsICJ0aW1lIjogdGltZSwKICAgICAgICAib3BlbiI6IF9zZWNfb3BlbiwgImZvcm1hdCI6IF9zZWNfZm9ybWF0LAogICAgICAgICJUcnVlIjogVHJ1ZSwgIkZhbHNlIjogRmFsc2UsICJOb25lIjogTm9uZSwKICAgICAgICAiaW50IjogaW50LCAiZmxvYXQiOiBmbG9hdCwgInN0ciI6IHN0ciwgImJvb2wiOiBib29sLAogICAgICAgICJsaXN0IjogbGlzdCwgImRpY3QiOiBkaWN0LCAidHVwbGUiOiB0dXBsZSwgInNldCI6IHNldCwgImZyb3plbnNldCI6IGZyb3plbnNldCwKICAgICAgICAiYnl0ZXMiOiBieXRlcywgImJ5dGVhcnJheSI6IGJ5dGVhcnJheSwKICAgICAgICAicmFuZ2UiOiByYW5nZSwgImxlbiI6IGxlbiwgIm1pbiI6IG1pbiwgIm1heCI6IG1heCwgInN1bSI6IHN1bSwgImFicyI6IGFicywKICAgICAgICAic29ydGVkIjogc29ydGVkLCAicmV2ZXJzZWQiOiByZXZlcnNlZCwgImVudW1lcmF0ZSI6IGVudW1lcmF0ZSwgInppcCI6IHppcCwKICAgICAgICAibWFwIjogbWFwLCAiZmlsdGVyIjogZmlsdGVyLCAiYWxsIjogYWxsLCAiYW55IjogYW55LAogICAgICAgICJpc2luc3RhbmNlIjogaXNpbnN0YW5jZSwgImlzc3ViY2xhc3MiOiBpc3N1YmNsYXNzLCAiY2FsbGFibGUiOiBjYWxsYWJsZSwKICAgICAgICAiaXRlciI6IGl0ZXIsICJuZXh0IjogbmV4dCwgInJlcHIiOiByZXByLCAiaGFzaCI6IGhhc2gsICJpZCI6IGlkLAogICAgICAgICJjaHIiOiBjaHIsICJvcmQiOiBvcmQsICJoZXgiOiBoZXgsICJiaW4iOiBiaW4sICJvY3QiOiBvY3QsCiAgICAgICAgImRpdm1vZCI6IGRpdm1vZCwgInBvdyI6IHBvdywgInJvdW5kIjogcm91bmQsCiAgICAgICAgIkV4Y2VwdGlvbiI6IEV4Y2VwdGlvbiwgIlZhbHVlRXJyb3IiOiBWYWx1ZUVycm9yLCAiVHlwZUVycm9yIjogVHlwZUVycm9yLAogICAgICAgICJLZXlFcnJvciI6IEtleUVycm9yLCAiSW5kZXhFcnJvciI6IEluZGV4RXJyb3IsICJSdW50aW1lRXJyb3IiOiBSdW50aW1lRXJyb3IsCiAgICAgICAgIlplcm9EaXZpc2lvbkVycm9yIjogWmVyb0RpdmlzaW9uRXJyb3IsICJBdHRyaWJ1dGVFcnJvciI6IEF0dHJpYnV0ZUVycm9yLAogICAgICAgICJOYW1lRXJyb3IiOiBOYW1lRXJyb3IsICJJbXBvcnRFcnJvciI6IEltcG9ydEVycm9yLCAiU3RvcEl0ZXJhdGlvbiI6IFN0b3BJdGVyYXRpb24sCiAgICAgICAgIkFyaXRobWV0aWNFcnJvciI6IEFyaXRobWV0aWNFcnJvciwgIkxvb2t1cEVycm9yIjogTG9va3VwRXJyb3IsCiAgICAgICAgIk5vdEltcGxlbWVudGVkRXJyb3IiOiBOb3RJbXBsZW1lbnRlZEVycm9yLCAiUGVybWlzc2lvbkVycm9yIjogUGVybWlzc2lvbkVycm9yLAogICAgICAgICJFT0ZFcnJvciI6IEVPRkVycm9yLCAiRmlsZU5vdEZvdW5kRXJyb3IiOiBGaWxlTm90Rm91bmRFcnJvciwKICAgICAgICAiT1NFcnJvciI6IE9TRXJyb3IsICJJT0Vycm9yIjogSU9FcnJvciwKICAgIH0pCgogICAgQkxPQ0tFRF9QUkVGSVhFUyA9ICgKICAgICAgICAib3MuIiwgInN1YnByb2Nlc3MuIiwgInNvY2tldC4iLCAiY3R5cGVzLiIsICJzaHV0aWwuIiwgInRlbXBmaWxlLiIsCiAgICAgICAgIndpbnJlZy4iLCAiX3dpbmFwaS4iLCAibW1hcC4iLCAic3FsaXRlMy4iLCAidXJsbGliLiIsICJodHRwLiIsCiAgICAgICAgImZ0cGxpYi4iLCAic210cGxpYi4iLCAidGVsbmV0bGliLiIsICJpbWFwbGliLiIsICJwb3BsaWIuIiwgIm5udHBsaWIuIiwKICAgICAgICAicGlja2xlLiIsICJtYXJzaGFsLiIsICJzaGVsdmUuIiwgImRibS4iLCAiemlwZmlsZS4iLCAidGFyZmlsZS4iLAogICAgICAgICJnemlwLiIsICJiejIuIiwgImx6bWEuIiwKICAgICkKICAgIF9ob29rX2VuYWJsZWQgPSBbRmFsc2VdCiAgICBfYWxsb3dfZXhlYyA9IFtGYWxzZV0KCiAgICBkZWYgX2F1ZGl0X2hvb2soZXZlbnQsIGV2X2FyZ3MpOgogICAgICAgIGlmIG5vdCBfaG9va19lbmFibGVkWzBdOgogICAgICAgICAgICByZXR1cm4KICAgICAgICBpZiBldmVudCA9PSAiZXhlYyIgYW5kIF9hbGxvd19leGVjWzBdOgogICAgICAgICAgICBfYWxsb3dfZXhlY1swXSA9IEZhbHNlCiAgICAgICAgICAgIHJldHVybgogICAgICAgIGlmIGV2ZW50IGluICgiZXhlYyIsICJjb21waWxlIik6CiAgICAgICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCAlcyIgJSBldmVudCkKICAgICAgICBpZiBldmVudCA9PSAib3BlbiI6CiAgICAgICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCBtbyBmaWxlIikKICAgICAgICBpZiBldmVudCA9PSAiaW1wb3J0IjoKICAgICAgICAgICAgbW9kID0gKGV2X2FyZ3NbMF0gb3IgIiIpLnNwbGl0KCIuIilbMF0gaWYgZXZfYXJncyBlbHNlICIiCiAgICAgICAgICAgIGlmIG1vZCBub3QgaW4gQUxMT1dFRF9NT0RVTEVTOgogICAgICAgICAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwIGltcG9ydCAnJXMnIiAlIG1vZCkKICAgICAgICAgICAgcmV0dXJuCiAgICAgICAgaWYgZXZlbnQgaW4gKCJzeXMuYWRkYXVkaXRob29rIiwgInN5cy5zZXR0cmFjZSIsICJzeXMuc2V0cHJvZmlsZSIpOgogICAgICAgICAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBLaG9uZyBkdW9jIHBoZXAgJXMiICUgZXZlbnQpCiAgICAgICAgaWYgZXZlbnQuc3RhcnRzd2l0aChCTE9DS0VEX1BSRUZJWEVTKToKICAgICAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwICVzIiAlIGV2ZW50KQoKICAgIHN5cy5hZGRhdWRpdGhvb2soX2F1ZGl0X2hvb2spCiAgICBzdGRvdXRfY2FwdHVyZSA9IGlvLlN0cmluZ0lPKCkKICAgIHN0ZGVycl9jYXB0dXJlID0gaW8uU3RyaW5nSU8oKQogICAgc3lzLnN0ZG91dCA9IHN0ZG91dF9jYXB0dXJlCiAgICBzeXMuc3RkZXJyID0gc3RkZXJyX2NhcHR1cmUKCiAgICBfaG9va19lbmFibGVkWzBdID0gVHJ1ZQogICAgX2FsbG93X2V4ZWNbMF0gPSBUcnVlCiAgICB0cnk6CiAgICAgICAgX2cgPSB7Il9fYnVpbHRpbnNfXyI6IHNhZmVfYnVpbHRpbnMsICJfX25hbWVfXyI6ICJfX21haW5fXyJ9CiAgICAgICAgdHJ5OgogICAgICAgICAgICBleGVjKGNvZGVfb2JqLCBfZykKICAgICAgICBmaW5hbGx5OgogICAgICAgICAgICBfYWxsb3dfZXhlY1swXSA9IEZhbHNlCiAgICBleGNlcHQgU2VjdXJpdHlFcnJvciBhcyBlOgogICAgICAgIHN5cy5zdGRlcnIud3JpdGUoc3RyKGUpKQogICAgZXhjZXB0IE1lbW9yeUVycm9yOgogICAgICAgIHN5cy5zdGRlcnIud3JpdGUoIlZ1b3QgcXVhIGdpb2kgaGFuIGJvIG5obyAxR0IiKQogICAgZXhjZXB0IEJhc2VFeGNlcHRpb24gYXMgZToKICAgICAgICBfaG9va19lbmFibGVkWzBdID0gRmFsc2UKICAgICAgICBfYWxsb3dfZXhlY1swXSA9IEZhbHNlCiAgICAgICAgdHJ5OgogICAgICAgICAgICBtc2cgPSAiJXM6ICVzIiAlICh0eXBlKGUpLl9fbmFtZV9fLCBzdHIoZSkgaWYgc3RyKGUpIGVsc2UgIihubyBtZXNzYWdlKSIpCiAgICAgICAgICAgIHN5cy5zdGRlcnIud3JpdGUobXNnKQogICAgICAgIGV4Y2VwdCBFeGNlcHRpb246CiAgICAgICAgICAgIHN5cy5zdGRlcnIud3JpdGUodHlwZShlKS5fX25hbWVfXykKICAgIGZpbmFsbHk6CiAgICAgICAgX2hvb2tfZW5hYmxlZFswXSA9IEZhbHNlCgogICAgX291dCA9IHN0ZG91dF9jYXB0dXJlLmdldHZhbHVlKCkKICAgIF9lcnIgPSBzdGRlcnJfY2FwdHVyZS5nZXR2YWx1ZSgpCiAgICBzeXMuc3Rkb3V0ID0gc3lzLl9fc3Rkb3V0X18KICAgIHN5cy5zdGRlcnIgPSBzeXMuX19zdGRlcnJfXwogICAgcHJpbnQoanNvbi5kdW1wcyh7Im91dCI6IF9vdXQsICJlcnIiOiBfZXJyfSkpCgpleGNlcHQgQmFzZUV4Y2VwdGlvbiBhcyBnbG9iYWxfZXJyOgogICAgIyBMVU9JIEFOIFRPQU4gQ1VPSSBDVU5HOiBOZXUgY28gYmF0IGt5IGxvaSBnaSB4YXkgcmEsIExVT04gaW4gcmEgSlNPTiBkZSBOb2RlLmpzIGRvYyBkdW9jCiAgICB0cnk6CiAgICAgICAgc3lzLnN0ZG91dCA9IHN5cy5fX3N0ZG91dF9fCiAgICAgICAgbXNnID0gIkxvaSBoZSB0aG9uZyBTYW5kYm94OiAlczogJXMiICUgKHR5cGUoZ2xvYmFsX2VycikuX19uYW1lX18sIHN0cihnbG9iYWxfZXJyKSkKICAgICAgICBwcmludChqc29uLmR1bXBzKHsib3V0IjogIiIsICJlcnIiOiBtc2d9KSkKICAgIGV4Y2VwdCBFeGNlcHRpb246CiAgICAgICAgcGFzcw==", "base64").toString("utf8");

        const pyExe = process.env.PYTHON_EXE
            || (process.platform === "win32" ? "python" : "python3");

        const py = spawn(pyExe, ["-I", "-S", "-c", wrapper], {
            timeout: 8000,
            windowsHide: true,
        });

        let outData = "", errData = "";
        py.stdout.on("data", d => outData += d.toString("utf8"));
        py.stderr.on("data", d => errData += d.toString("utf8"));

        py.on("error", (err) => {
            emitResponse(id, false, "Loi: Khong tim thay Python trong PATH.");
        });

        py.on("close", (exitCode) => {
            if (outData.trim()) {
                try {
                    const res = JSON.parse(outData.trim());
                    if (res.err && res.err.trim()) {
                        return emitResponse(id, false, res.err.trim());
                    }
                    let cleanOut = res.out || "";
                    if (cleanOut.length > 8000) {
                        cleanOut = cleanOut.slice(0, 8000) + "\n...(da cat bot)";
                    }
                    return emitResponse(id, true, { output: cleanOut });
                } catch (_) {}
            }
            if (errData.trim()) {
                return emitResponse(id, false, errData.trim());
            }
            if (exitCode === null) {
                return emitResponse(id, false, "Qua thoi gian thuc thi (8 giay)");
            }
            return emitResponse(id, false, "Tien trinh bi kill (co the vuot 1GB hoac crash)");
        });

        py.stdin.write(JSON.stringify({
            code: pyCode,
            args: pyArgs,
            admin: isAdmin,
            suspicion: suspicion,
        }));
        py.stdin.end();
      } catch (e) {
        emitResponse(id, false, "Loi Node.js: " + e.message);
      }
    }
        else if (cmd.action === "ping") {
      emitResponse(id, true, "pong");
    }
    else {
      emitResponse(id, false, "unknown action: " + cmd.action);
    }
  } catch (e) {
    log("action " + cmd.action + " ERROR: " + (e?.message || String(e)));
    emitResponse(id, false, e?.message || String(e));
  }
});

rl.on("close", () => {
  log("stdin closed, thoÃƒÂ¡t");
  process.exit(0);
});
