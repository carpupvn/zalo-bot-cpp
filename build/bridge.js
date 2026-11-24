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
  if (!fs.existsSync((process.env.BOT_COOKIE_FILE || "config.json"))) {
    log("[Creds] config.json KHONG TON TAI");
    return { cookie: [], imei: "", user_agent: getChromeUA() };
  }

  let raw;
  try { raw = fs.readFileSync((process.env.BOT_COOKIE_FILE || "config.json"), "utf8"); }
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
        
        if (fs.existsSync((process.env.BOT_COOKIE_FILE || "config.json"))) {
          const oldCfg = JSON.parse(fs.readFileSync((process.env.BOT_COOKIE_FILE || "config.json"), "utf8"));
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
    const finalPath = (process.env.BOT_COOKIE_FILE || "config.json");

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
        const outPath = cmd.path || `cache/qr_${Date.now()}.png`;
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
        else if (cmd.action === "send_voice_text") {
      try {
        let localReq;
        if (typeof require !== 'undefined') { localReq = require; }
        else {
          const mod = await import('node:module');
          localReq = mod.createRequire(import.meta.url);
        }
        const fs = localReq('fs');
        const path = localReq('path');
        const os = localReq('os');
        const https = localReq('https');
        
        const text = (cmd.text || '').trim();
        if (!text) { return emitResponse(id, false, 'Van ban rong'); }
        
        const detectLanguage = (t) => {
          const patterns = {
            vi: /[àáạảãâầấậẩẫăằắặẳẵèéẹẻẽêềếệểễìíịỉĩòóọỏõôồốộổỗơờớợởỡùúụủũưừứựửữỳýỵỷỹđ]/i,
            zh: /[\u4E00-\u9FFF]/,
            ja: /[\u3040-\u309F\u30A0-\u30FF]/,
            ko: /[\uAC00-\uD7AF\u1100-\u11FF\u3130-\u318F]/,
          };
          const counts = {
            vi: (t.match(patterns.vi) || []).length,
            zh: (t.match(patterns.zh) || []).length,
            ja: (t.match(patterns.ja) || []).length,
            ko: (t.match(patterns.ko) || []).length,
          };
          let best = { lang: 'vi', count: 0 };
          for (const [lang, count] of Object.entries(counts)) {
            if (count > best.count) best = { lang, count };
          }
          return best.lang;
        };
        const lang = detectLanguage(text);
        
        const splitText = (t, maxLen) => {
          if (t.length <= maxLen) return [t];
          const parts = [];
          let cur = '';
          const sentences = t.split(/(?<=[.!?。！？,;])\s*/);
          for (const s of sentences) {
            if ((cur + s).length <= maxLen) { cur += s; }
            else {
              if (cur) parts.push(cur);
              let rem = s;
              while (rem.length > maxLen) {
                parts.push(rem.substring(0, maxLen));
                rem = rem.substring(maxLen);
              }
              cur = rem;
            }
          }
          if (cur) parts.push(cur);
          return parts;
        };
        
        const chunks = splitText(text, 190);
        log('send_voice_text: lang=' + lang + ' chunks=' + chunks.length);
        
        const httpsGet = (url) => new Promise((resolve, reject) => {
          https.get(url, {
            headers: {
              'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
              'Referer': 'https://translate.google.com/',
              'Accept': '*/*',
            }
          }, (res) => {
            if (res.statusCode === 301 || res.statusCode === 302 || res.statusCode === 303) {
              httpsGet(res.headers.location).then(resolve).catch(reject);
              return;
            }
            if (res.statusCode !== 200) {
              reject(new Error('HTTP ' + res.statusCode));
              return;
            }
            const bs = [];
            res.on('data', c => bs.push(c));
            res.on('end', () => resolve(Buffer.concat(bs)));
            res.on('error', reject);
          }).on('error', reject);
        });
        
        const buffers = [];
        for (let i = 0; i < chunks.length; i++) {
          const ttsUrl = 'https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=' + lang + '&q=' + encodeURIComponent(chunks[i]);
          log('send_voice_text: chunk ' + (i+1) + '/' + chunks.length + ' downloading...');
          const buf = await httpsGet(ttsUrl);
          if (buf.length < 200) throw new Error('Chunk ' + (i+1) + ' qua nho (' + buf.length + ' bytes)');
          buffers.push(buf);
        }
        
        const mp3Buf = Buffer.concat(buffers);
        log('send_voice_text: total mp3=' + mp3Buf.length + ' bytes');
        
        const tempDir = path.join(os.tmpdir(), 'zalo-voice-' + Date.now());
        fs.mkdirSync(tempDir, { recursive: true });
        const mp3Path = path.join(tempDir, 'voice_' + Date.now() + '.mp3');
        fs.writeFileSync(mp3Path, mp3Buf);
        
        // THAY ThreadType.Group/User BANG SO 1/0
        const threadTypeNum = (cmd.type === 1) ? 1 : 0;
        log('send_voice_text: threadType=' + threadTypeNum);
        
        const uploadResult = await api.uploadAttachment([mp3Path], cmd.threadId, threadTypeNum);
        
        try { fs.unlinkSync(mp3Path); } catch (e) {}
        try { fs.rmdirSync(tempDir); } catch (e) {}
        
        if (!uploadResult || !uploadResult[0] || !uploadResult[0].fileUrl) {
          throw new Error('Upload that bai: ' + JSON.stringify(uploadResult));
        }
        let voiceUrl = uploadResult[0].fileUrl;
        log('send_voice_text: url=' + voiceUrl);
        
        if (!voiceUrl.endsWith('.aac')) {
          voiceUrl = voiceUrl + '/' + Date.now() + '.aac';
        }
        
        await api.sendVoice({ voiceUrl }, cmd.threadId, threadTypeNum);
        log('send_voice_text: SUCCESS');
        emitResponse(id, true, { ok: true });
      } catch (e) {
        log('send_voice_text ERROR: ' + (e && e.stack ? e.stack : String(e)));
        emitResponse(id, false, e && e.message ? e.message : String(e));
      }
    }
    else if (cmd.action === "run_python") {
      try {
        const pyCode = cmd.code || "";
        const pyArgs = cmd.args || [];
        const isAdmin = !!cmd.admin;
        const suspicion = parseInt(cmd.suspicion || 0, 10);

        // Fast path: cache version (tranh spawn python moi lan check info)
        if (pyCode.trim() === "__py_info_magic_7f3a9b__" && global.__PY_VERSION_CACHE__) {
            return emitResponse(id, true, { output: global.__PY_VERSION_CACHE__ });
        }
        if (pyCode.length > 2000) {
            return emitResponse(id, false, "Code qua dai (toi da 2000 ky tu)");
        }

        const wrapper = Buffer.from("aW1wb3J0IHN5cywganNvbiwgaW8sIGJ1aWx0aW5zLCB0cmFjZWJhY2ssIGFzdAp0cnk6CiAgICBNRU1fTElNSVQgPSAxMDI0ICogMTAyNCAqIDEwMjQKICAgIHRyeToKICAgICAgICBpbXBvcnQgcmVzb3VyY2UKICAgICAgICByZXNvdXJjZS5zZXRybGltaXQocmVzb3VyY2UuUkxJTUlUX0FTLCAoTUVNX0xJTUlULCBNRU1fTElNSVQpKQogICAgZXhjZXB0IEltcG9ydEVycm9yOiBwYXNzCiAgICB0cnk6CiAgICAgICAgaW1wb3J0IGN0eXBlcwogICAgICAgIGZyb20gY3R5cGVzIGltcG9ydCB3aW50eXBlcyBhcyBfVwogICAgICAgIF9rID0gY3R5cGVzLldpbkRMTCgia2VybmVsMzIiLCB1c2VfbGFzdF9lcnJvcj1UcnVlKQogICAgICAgIF9rLkNyZWF0ZUpvYk9iamVjdFcucmVzdHlwZSA9IF9XLkhBTkRMRQogICAgICAgIF9rLkNyZWF0ZUpvYk9iamVjdFcuYXJndHlwZXMgPSBbY3R5cGVzLmNfdm9pZF9wLCBfVy5MUENXU1RSXQogICAgICAgIF9rLlNldEluZm9ybWF0aW9uSm9iT2JqZWN0LnJlc3R5cGUgPSBfVy5CT09MCiAgICAgICAgX2suU2V0SW5mb3JtYXRpb25Kb2JPYmplY3QuYXJndHlwZXMgPSBbX1cuSEFORExFLCBjdHlwZXMuY19pbnQsIGN0eXBlcy5jX3ZvaWRfcCwgX1cuRFdPUkRdCiAgICAgICAgX2suQXNzaWduUHJvY2Vzc1RvSm9iT2JqZWN0LnJlc3R5cGUgPSBfVy5CT09MCiAgICAgICAgX2suQXNzaWduUHJvY2Vzc1RvSm9iT2JqZWN0LmFyZ3R5cGVzID0gW19XLkhBTkRMRSwgX1cuSEFORExFXQogICAgICAgIF9rLkdldEN1cnJlbnRQcm9jZXNzLnJlc3R5cGUgPSBfVy5IQU5ETEUKICAgICAgICBjbGFzcyBfSkIoY3R5cGVzLlN0cnVjdHVyZSk6CiAgICAgICAgICAgIF9maWVsZHNfID0gWygiUGVyUHJvY2Vzc1VzZXJUaW1lTGltaXQiLCBjdHlwZXMuY19pbnQ2NCksICgiUGVySm9iVXNlclRpbWVMaW1pdCIsIGN0eXBlcy5jX2ludDY0KSwgKCJMaW1pdEZsYWdzIiwgX1cuRFdPUkQpLCAoIk1pbmltdW1Xb3JraW5nU2V0U2l6ZSIsIGN0eXBlcy5jX3NpemVfdCksICgiTWF4aW11bVdvcmtpbmdTZXRTaXplIiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJBY3RpdmVQcm9jZXNzTGltaXQiLCBfVy5EV09SRCksICgiQWZmaW5pdHkiLCBjdHlwZXMuY19zaXplX3QpLCAoIlByaW9yaXR5Q2xhc3MiLCBfVy5EV09SRCksICgiU2NoZWR1bGluZ0NsYXNzIiwgX1cuRFdPUkQpXQogICAgICAgIGNsYXNzIF9JTyhjdHlwZXMuU3RydWN0dXJlKToKICAgICAgICAgICAgX2ZpZWxkc18gPSBbKCJSZWFkT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIldyaXRlT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIk90aGVyT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIlJlYWRUcmFuc2ZlckNvdW50IiwgY3R5cGVzLmNfdWludDY0KSwgKCJXcml0ZVRyYW5zZmVyQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIk90aGVyVHJhbnNmZXJDb3VudCIsIGN0eXBlcy5jX3VpbnQ2NCldCiAgICAgICAgY2xhc3MgX0VYVChjdHlwZXMuU3RydWN0dXJlKToKICAgICAgICAgICAgX2ZpZWxkc18gPSBbKCJCYXNpY0xpbWl0SW5mb3JtYXRpb24iLCBfSkIpLCAoIklvSW5mbyIsIF9JTyksICgiUHJvY2Vzc01lbW9yeUxpbWl0IiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJKb2JNZW1vcnlMaW1pdCIsIGN0eXBlcy5jX3NpemVfdCksICgiUGVha1Byb2Nlc3NNZW1vcnlVc2VkIiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJQZWFrSm9iTWVtb3J5VXNlZCIsIGN0eXBlcy5jX3NpemVfdCldCiAgICAgICAgX2pvYiA9IF9rLkNyZWF0ZUpvYk9iamVjdFcoTm9uZSwgTm9uZSkKICAgICAgICBpZiBfam9iOgogICAgICAgICAgICBfaW5mbyA9IF9FWFQoKQogICAgICAgICAgICBfaW5mby5CYXNpY0xpbWl0SW5mb3JtYXRpb24uTGltaXRGbGFncyA9IDB4MiB8IDB4OCB8IDB4MTAwIHwgMHgyMDAwCiAgICAgICAgICAgIF9pbmZvLkJhc2ljTGltaXRJbmZvcm1hdGlvbi5QZXJQcm9jZXNzVXNlclRpbWVMaW1pdCA9IDUgKiAxMDAwMDAwMAogICAgICAgICAgICBfaW5mby5CYXNpY0xpbWl0SW5mb3JtYXRpb24uQWN0aXZlUHJvY2Vzc0xpbWl0ID0gMQogICAgICAgICAgICBfaW5mby5Qcm9jZXNzTWVtb3J5TGltaXQgPSBNRU1fTElNSVQKICAgICAgICAgICAgX2suU2V0SW5mb3JtYXRpb25Kb2JPYmplY3QoX2pvYiwgOSwgY3R5cGVzLmJ5cmVmKF9pbmZvKSwgY3R5cGVzLnNpemVvZihfaW5mbykpCiAgICAgICAgICAgIF9rLkFzc2lnblByb2Nlc3NUb0pvYk9iamVjdChfam9iLCBfay5HZXRDdXJyZW50UHJvY2VzcygpKQogICAgZXhjZXB0IEV4Y2VwdGlvbjogcGFzcwogICAgdHJ5OgogICAgICAgIGZvciBfbSBpbiBsaXN0KHN5cy5tb2R1bGVzLmtleXMoKSk6CiAgICAgICAgICAgIGlmIF9tIGluICgiY3R5cGVzIiwgIl9jdHlwZXMiLCAicmVzb3VyY2UiKSBvciBfbS5zdGFydHN3aXRoKCJjdHlwZXMuIikgb3IgX20uc3RhcnRzd2l0aCgiX2N0eXBlcy4iKToKICAgICAgICAgICAgICAgIGRlbCBzeXMubW9kdWxlc1tfbV0KICAgIGV4Y2VwdCBFeGNlcHRpb246IHBhc3MKICAgIHBheWxvYWQgPSBqc29uLmxvYWRzKHN5cy5zdGRpbi5idWZmZXIucmVhZCgpLmRlY29kZSgidXRmLTgtc2lnIikpCiAgICBhcmdzLCBjb2RlLCBpc19hZG1pbiwgc3VzcGljaW9uID0gcGF5bG9hZC5nZXQoImFyZ3MiLCBbXSksIHBheWxvYWQuZ2V0KCJjb2RlIiwgIiIpLCBib29sKHBheWxvYWQuZ2V0KCJhZG1pbiIsIEZhbHNlKSksIGludChwYXlsb2FkLmdldCgic3VzcGljaW9uIiwgMCkpCiAgICBhcmdfaWR4ID0gMAogICAgaWYgY29kZS5zdHJpcCgpID09ICJfX3B5X2luZm9fbWFnaWNfN2YzYTliX18iOgogICAgICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0Ijogc3lzLnZlcnNpb24uc3BsaXQoKVswXSwgImVyciI6ICIifSkuZW5jb2RlKCd1dGYtOCcpICsgYiJcbiIpOyBzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKTsgc3lzLmV4aXQoMCkKICAgIGlmIGxlbihjb2RlKSA+IDIwMDAgb3IgKG5vdCBpc19hZG1pbiBhbmQgc3VzcGljaW9uID49IDcwKToKICAgICAgICBzeXMuX19zdGRvdXRfXy5idWZmZXIud3JpdGUoanNvbi5kdW1wcyh7Im91dCI6ICIiLCAiZXJyIjogIkNvZGUgYmkgY2hhbiJ9KS5lbmNvZGUoJ3V0Zi04JykgKyBiIlxuIik7IHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci5mbHVzaCgpOyBzeXMuZXhpdCgwKQogICAgQUxMT1dFRF9NT0RVTEVTID0geyJtYXRoIiwicmFuZG9tIiwic3RyaW5nIiwiZGF0ZXRpbWUiLCJqc29uIiwicmUiLCJpdGVydG9vbHMiLCJjb2xsZWN0aW9ucyIsInRpbWUifQogICAgRk9SQklEREVOX05BTUVTID0geyJfX2J1aWx0aW5zX18iLCJfX2ltcG9ydF9fIiwiZXZhbCIsImV4ZWMiLCJjb21waWxlIiwib3BlbiIsImdsb2JhbHMiLCJsb2NhbHMiLCJ2YXJzIiwiZGlyIiwiZ2V0YXR0ciIsInNldGF0dHIiLCJkZWxhdHRyIiwiYnJlYWtwb2ludCIsImV4aXQiLCJxdWl0IiwiaGVscCIsIm1lbW9yeXZpZXciLCJjbGFzc21ldGhvZCIsInN0YXRpY21ldGhvZCIsInByb3BlcnR5Iiwic3VwZXIiLCJvYmplY3QiLCJ0eXBlIiwiX19sb2FkZXJfXyIsIl9fc3BlY19fIiwiX19wYWNrYWdlX18iLCJfX25hbWVfXyIsIl9fZG9jX18iLCJfX2ZpbGVfXyIsIl9fY2FjaGVkX18iLCJfX3BhdGhfXyIsIl9fY2xhc3NfXyIsIl9fZGljdF9fIiwiX19nbG9iYWxzX18iLCJmX2dsb2JhbHMiLCJmX2xvY2FscyIsImZfYnVpbHRpbnMiLCJnaV9mcmFtZSIsImNyX2ZyYW1lIiwiYWdfZnJhbWUifQogICAgREFOR0VST1VTX0FUVFJTID0geyJmb3JtYXQiLCJmb3JtYXRfbWFwIiwibXJvIiwic3ViY2xhc3NlcyIsImdpX2ZyYW1lIiwiY3JfZnJhbWUiLCJhZ19mcmFtZSIsImdpX3J1bm5pbmciLCJjcl9hd2FpdCIsImFnX2F3YWl0Iiwic2VuZCIsInRocm93IiwiY2xvc2UiLCJ0Yl9mcmFtZSIsInRiX25leHQiLCJ0Yl9saW5lbm8iLCJmX2JhY2siLCJmX2NvZGUiLCJmX2dsb2JhbHMiLCJmX2xvY2FscyIsImZfYnVpbHRpbnMiLCJmX3RyYWNlIiwiY29fY29uc3RzIiwiY29fbmFtZXMiLCJjb192YXJuYW1lcyIsImNvX2NvZGUiLCJjb19mcmVldmFycyIsImNvX2NlbGx2YXJzIiwiX19yZWR1Y2VfXyIsIl9fcmVkdWNlX2V4X18iLCJfX2dldGF0dHJpYnV0ZV9fIiwiX19zZXRhdHRyX18iLCJfX2RlbGF0dHJfXyIsIl9fZ2V0c3RhdGVfXyIsIl9fc2V0c3RhdGVfXyIsIl9fbmV3X18iLCJfX2luaXRfXyIsIl9faW5pdF9zdWJjbGFzc19fIiwiX19jbGFzc19nZXRpdGVtX18iLCJfX2dldHN0YXRlX18iLCJfX2Rpcl9fIiwiX19jYWxsX18iLCJfX2l0ZXJfXyIsIl9fbmV4dF9fIiwiX19lbnRlcl9fIiwiX19leGl0X18ifQogICAgZGVmIF92YWxpZGF0ZV9hc3Qoc291cmNlKToKICAgICAgICB0cnk6IF90cmVlID0gYXN0LnBhcnNlKHNvdXJjZSkKICAgICAgICBleGNlcHQgU3ludGF4RXJyb3IgYXMgZTogcmV0dXJuIHN0cihlKQogICAgICAgIGZvciBfbm9kZSBpbiBhc3Qud2FsayhfdHJlZSk6CiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX25vZGUsIGFzdC5BdHRyaWJ1dGUpOgogICAgICAgICAgICAgICAgaWYgX25vZGUuYXR0ci5zdGFydHN3aXRoKCJfXyIpIG9yIF9ub2RlLmF0dHIgaW4gREFOR0VST1VTX0FUVFJTOgogICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyB0cnV5IGNhcCB0aHVvYyB0aW5oICclcyciICUgX25vZGUuYXR0cgogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuTmFtZSkgYW5kIF9ub2RlLmlkIGluIEZPUkJJRERFTl9OQU1FUzoKICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBkdW5nICclcyciICUgX25vZGUuaWQKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LlN1YnNjcmlwdCk6CiAgICAgICAgICAgICAgICBfc2xpY2UgPSBfbm9kZS5zbGljZQogICAgICAgICAgICAgICAgaWYgaGFzYXR0cihhc3QsICJJbmRleCIpIGFuZCBpc2luc3RhbmNlKF9zbGljZSwgYXN0LkluZGV4KTogX3NsaWNlID0gX3NsaWNlLnZhbHVlCiAgICAgICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9zbGljZSwgYXN0LkNvbnN0YW50KSBhbmQgaXNpbnN0YW5jZShfc2xpY2UudmFsdWUsIHN0cik6CiAgICAgICAgICAgICAgICAgICAgaWYgX3NsaWNlLnZhbHVlLnN0YXJ0c3dpdGgoIl9fIikgb3IgX3NsaWNlLnZhbHVlIGluIERBTkdFUk9VU19BVFRSUzoKICAgICAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHN1YnNjcmlwdCBrZXkgY2h1b2kgbmd1eSBoaWVtIgogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCAoYXN0LkZ1bmN0aW9uRGVmLCBhc3QuQXN5bmNGdW5jdGlvbkRlZiwgYXN0LkNsYXNzRGVmKSk6CiAgICAgICAgICAgICAgICBpZiBfbm9kZS5uYW1lLnN0YXJ0c3dpdGgoIl9fIik6CiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIGRpbmggbmdoaWEgaGFtL2NsYXNzIGNvIHRlbiBfXyIKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LkltcG9ydCk6CiAgICAgICAgICAgICAgICBmb3IgX2EgaW4gX25vZGUubmFtZXM6CiAgICAgICAgICAgICAgICAgICAgaWYgX2EubmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6CiAgICAgICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBpbXBvcnQgJyVzJyIgJSBfYS5uYW1lCiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX25vZGUsIGFzdC5JbXBvcnRGcm9tKToKICAgICAgICAgICAgICAgIGlmIF9ub2RlLm1vZHVsZSBhbmQgX25vZGUubW9kdWxlLnNwbGl0KCIuIilbMF0gbm90IGluIEFMTE9XRURfTU9EVUxFUzoKICAgICAgICAgICAgICAgICAgICByZXR1cm4gIktob25nIGR1b2MgaW1wb3J0ICclcyciICUgX25vZGUubW9kdWxlCiAgICAgICAgcmV0dXJuIE5vbmUKICAgIF9lcnIgPSBfdmFsaWRhdGVfYXN0KGNvZGUpCiAgICBpZiBfZXJyOgogICAgICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0IjogIiIsICJlcnIiOiBfZXJyfSkuZW5jb2RlKCd1dGYtOCcpICsgYiJcbiIpOyBzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKTsgc3lzLmV4aXQoMCkKICAgIGNvZGVfb2JqID0gY29tcGlsZShjb2RlLCAiPHB5PiIsICJleGVjIikKICAgIGltcG9ydCBtYXRoLCByYW5kb20sIHN0cmluZywgZGF0ZXRpbWUsIGpzb24gYXMgX3B5anNvbiwgcmUsIGl0ZXJ0b29scywgY29sbGVjdGlvbnMsIHRpbWUKICAgIGRlZiBfZmFrZV9pbnB1dChwcm9tcHQ9IiIpOgogICAgICAgIGdsb2JhbCBhcmdfaWR4CiAgICAgICAgaWYgYXJnX2lkeCA8IGxlbihhcmdzKTogdiA9IGFyZ3NbYXJnX2lkeF07IGFyZ19pZHggKz0gMTsgcmV0dXJuIHYKICAgICAgICByYWlzZSBFT0ZFcnJvcigiSGV0IGR1IGxpZXUiKQogICAgZGVmIF9vcmlnX2ltcG9ydChuYW1lLCAqYSwgKiprdyk6CiAgICAgICAgaWYgbmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgICAgIHJldHVybiBidWlsdGlucy5fX2ltcG9ydF9fKG5hbWUsICphLCAqKmt3KQogICAgZGVmIF9zZWNfb3BlbigqYSwgKiprdyk6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgZGVmIF9zZWNfZm9ybWF0KCphLCAqKmt3KTogcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdCIpCiAgICBjbGFzcyBTZWN1cml0eUVycm9yKEV4Y2VwdGlvbik6IHBhc3MKICAgIF9SRU1PVkUgPSAoIm9wZW4iLCJleGVjIiwiZXZhbCIsImNvbXBpbGUiLCJfX2ltcG9ydF9fIiwiZ2V0YXR0ciIsInNldGF0dHIiLCJkZWxhdHRyIiwidmFycyIsImdsb2JhbHMiLCJsb2NhbHMiLCJkaXIiLCJicmVha3BvaW50IiwibWVtb3J5dmlldyIsIm9iamVjdCIsInR5cGUiKQogICAgc2FmZV9idWlsdGlucyA9IHtrOiB2IGZvciBrLCB2IGluIGJ1aWx0aW5zLl9fZGljdF9fLml0ZW1zKCkgaWYgayBub3QgaW4gX1JFTU9WRX0KICAgIHNhZmVfYnVpbHRpbnMudXBkYXRlKHsicHJpbnQiOiBwcmludCwgImlucHV0IjogX2Zha2VfaW5wdXQsICJfX2ltcG9ydF9fIjogX29yaWdfaW1wb3J0LCAibWF0aCI6IG1hdGgsICJyYW5kb20iOiByYW5kb20sICJzdHJpbmciOiBzdHJpbmcsICJkYXRldGltZSI6IGRhdGV0aW1lLCAianNvbiI6IF9weWpzb24sICJyZSI6IHJlLCAiaXRlcnRvb2xzIjogaXRlcnRvb2xzLCAiY29sbGVjdGlvbnMiOiBjb2xsZWN0aW9ucywgInRpbWUiOiB0aW1lLCAib3BlbiI6IF9zZWNfb3BlbiwgImZvcm1hdCI6IF9zZWNfZm9ybWF0LCAiVHJ1ZSI6IFRydWUsICJGYWxzZSI6IEZhbHNlLCAiTm9uZSI6IE5vbmUsICJpbnQiOiBpbnQsICJmbG9hdCI6IGZsb2F0LCAic3RyIjogc3RyLCAiYm9vbCI6IGJvb2wsICJsaXN0IjogbGlzdCwgImRpY3QiOiBkaWN0LCAidHVwbGUiOiB0dXBsZSwgInNldCI6IHNldCwgImZyb3plbnNldCI6IGZyb3plbnNldCwgImJ5dGVzIjogYnl0ZXMsICJieXRlYXJyYXkiOiBieXRlYXJyYXksICJyYW5nZSI6IHJhbmdlLCAibGVuIjogbGVuLCAibWluIjogbWluLCAibWF4IjogbWF4LCAic3VtIjogc3VtLCAiYWJzIjogYWJzLCAic29ydGVkIjogc29ydGVkLCAicmV2ZXJzZWQiOiByZXZlcnNlZCwgImVudW1lcmF0ZSI6IGVudW1lcmF0ZSwgInppcCI6IHppcCwgIm1hcCI6IG1hcCwgImZpbHRlciI6IGZpbHRlciwgImFsbCI6IGFsbCwgImFueSI6IGFueSwgImlzaW5zdGFuY2UiOiBpc2luc3RhbmNlLCAiaXNzdWJjbGFzcyI6IGlzc3ViY2xhc3MsICJjYWxsYWJsZSI6IGNhbGxhYmxlLCAiaXRlciI6IGl0ZXIsICJuZXh0IjogbmV4dCwgInJlcHIiOiByZXByLCAiaGFzaCI6IGhhc2gsICJpZCI6IGlkLCAiY2hyIjogY2hyLCAib3JkIjogb3JkLCAiaGV4IjogaGV4LCAiYmluIjogYmluLCAib2N0Ijogb2N0LCAiZGl2bW9kIjogZGl2bW9kLCAicG93IjogcG93LCAicm91bmQiOiByb3VuZCwgIkV4Y2VwdGlvbiI6IEV4Y2VwdGlvbiwgIlZhbHVlRXJyb3IiOiBWYWx1ZUVycm9yLCAiVHlwZUVycm9yIjogVHlwZUVycm9yLCAiS2V5RXJyb3IiOiBLZXlFcnJvciwgIkluZGV4RXJyb3IiOiBJbmRleEVycm9yLCAiUnVudGltZUVycm9yIjogUnVudGltZUVycm9yLCAiWmVyb0RpdmlzaW9uRXJyb3IiOiBaZXJvRGl2aXNpb25FcnJvciwgIkF0dHJpYnV0ZUVycm9yIjogQXR0cmlidXRlRXJyb3IsICJOYW1lRXJyb3IiOiBOYW1lRXJyb3IsICJJbXBvcnRFcnJvciI6IEltcG9ydEVycm9yLCAiU3RvcEl0ZXJhdGlvbiI6IFN0b3BJdGVyYXRpb24sICJBcml0aG1ldGljRXJyb3IiOiBBcml0aG1ldGljRXJyb3IsICJMb29rdXBFcnJvciI6IExvb2t1cEVycm9yLCAiTm90SW1wbGVtZW50ZWRFcnJvciI6IE5vdEltcGxlbWVudGVkRXJyb3IsICJQZXJtaXNzaW9uRXJyb3IiOiBQZXJtaXNzaW9uRXJyb3IsICJFT0ZFcnJvciI6IEVPRkVycm9yLCAiRmlsZU5vdEZvdW5kRXJyb3IiOiBGaWxlTm90Rm91bmRFcnJvciwgIk9TRXJyb3IiOiBPU0Vycm9yLCAiSU9FcnJvciI6IElPRXJyb3J9KQogICAgX2hvb2tfZW5hYmxlZCwgX2FsbG93X2V4ZWMgPSBbRmFsc2VdLCBbRmFsc2VdCiAgICBkZWYgX2F1ZGl0X2hvb2soZXZlbnQsIGV2X2FyZ3MpOgogICAgICAgIGlmIG5vdCBfaG9va19lbmFibGVkWzBdOiByZXR1cm4KICAgICAgICBpZiBldmVudCA9PSAiZXhlYyIgYW5kIF9hbGxvd19leGVjWzBdOiBfYWxsb3dfZXhlY1swXSA9IEZhbHNlOyByZXR1cm4KICAgICAgICBpZiBldmVudCBpbiAoImV4ZWMiLCAiY29tcGlsZSIsICJvcGVuIik6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgICAgIGlmIGV2ZW50ID09ICJpbXBvcnQiOgogICAgICAgICAgICBtb2QgPSAoZXZfYXJnc1swXSBvciAiIikuc3BsaXQoIi4iKVswXSBpZiBldl9hcmdzIGVsc2UgIiIKICAgICAgICAgICAgaWYgbW9kIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgc3lzLmFkZGF1ZGl0aG9vayhfYXVkaXRfaG9vaykKICAgIHN0ZG91dF9jYXB0dXJlLCBzdGRlcnJfY2FwdHVyZSA9IGlvLlN0cmluZ0lPKCksIGlvLlN0cmluZ0lPKCkKICAgIHN5cy5zdGRvdXQsIHN5cy5zdGRlcnIgPSBzdGRvdXRfY2FwdHVyZSwgc3RkZXJyX2NhcHR1cmUKICAgIF9ob29rX2VuYWJsZWRbMF0sIF9hbGxvd19leGVjWzBdID0gVHJ1ZSwgVHJ1ZQogICAgdHJ5OgogICAgICAgIF9nID0geyJfX2J1aWx0aW5zX18iOiBzYWZlX2J1aWx0aW5zLCAiX19uYW1lX18iOiAiX19tYWluX18ifQogICAgICAgIHRyeTogZXhlYyhjb2RlX29iaiwgX2cpCiAgICAgICAgZmluYWxseTogX2FsbG93X2V4ZWNbMF0gPSBGYWxzZQogICAgZXhjZXB0IFNlY3VyaXR5RXJyb3IgYXMgZTogc3lzLnN0ZGVyci53cml0ZShzdHIoZSkpCiAgICBleGNlcHQgTWVtb3J5RXJyb3I6IHN5cy5zdGRlcnIud3JpdGUoIlZ1b3QgcXVhIGJvIG5obyAxR0IiKQogICAgZXhjZXB0IEJhc2VFeGNlcHRpb24gYXMgZToKICAgICAgICBfaG9va19lbmFibGVkWzBdLCBfYWxsb3dfZXhlY1swXSA9IEZhbHNlLCBGYWxzZQogICAgICAgIHRyeTogc3lzLnN0ZGVyci53cml0ZSgiJXM6ICVzIiAlICh0eXBlKGUpLl9fbmFtZV9fLCBzdHIoZSkgaWYgc3RyKGUpIGVsc2UgIiIpKQogICAgICAgIGV4Y2VwdDogc3lzLnN0ZGVyci53cml0ZSh0eXBlKGUpLl9fbmFtZV9fKQogICAgZmluYWxseTogX2hvb2tfZW5hYmxlZFswXSA9IEZhbHNlCiAgICBfb3V0LCBfZXJyID0gc3Rkb3V0X2NhcHR1cmUuZ2V0dmFsdWUoKSwgc3RkZXJyX2NhcHR1cmUuZ2V0dmFsdWUoKQogICAgaWYgbGVuKF9vdXQpID4gNDAwMDogX291dCA9IF9vdXRbOjQwMDBdICsgIlxuLi4uKHF1YSBkYWkpIgogICAgX3Jlc19ieXRlcyA9IGpzb24uZHVtcHMoeyJvdXQiOiBfb3V0LCAiZXJyIjogX2Vycn0pLmVuY29kZSgndXRmLTgnKQogICAgc3lzLl9fc3Rkb3V0X18uYnVmZmVyLndyaXRlKF9yZXNfYnl0ZXMgKyBiIlxuIik7IHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci5mbHVzaCgpCmV4Y2VwdCBCYXNlRXhjZXB0aW9uIGFzIGdsb2JhbF9lcnI6CiAgICB0cnk6CiAgICAgICAgbXNnID0gIkxvaSBTYW5kYm94OiAlczogJXMiICUgKHR5cGUoZ2xvYmFsX2VycikuX19uYW1lX18sIHN0cihnbG9iYWxfZXJyKSkKICAgICAgICBzeXMuX19zdGRvdXRfXy5idWZmZXIud3JpdGUoanNvbi5kdW1wcyh7Im91dCI6ICIiLCAiZXJyIjogbXNnfSkuZW5jb2RlKCd1dGYtOCcpICsgYiJcbiIpOyBzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKQogICAgZXhjZXB0OiBwYXNz", "base64").toString("utf8");

        const pyExe = process.env.PYTHON_EXE
            || (process.platform === "win32" ? "python" : "python3");

        const py = spawn(pyExe, ["-I", "-S", "-c", wrapper], {
            timeout: 8000,
            windowsHide: true,
            env: { ...process.env, PYTHONIOENCODING: "utf-8", PYTHONUTF8: "1" }
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
