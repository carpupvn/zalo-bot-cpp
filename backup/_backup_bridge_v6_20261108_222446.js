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

        const wrapper = Buffer.from("aW1wb3J0IHN5cywganNvbiwgaW8sIGJ1aWx0aW5zLCB0cmFjZWJhY2ssIGFzdApNRU1fTElNSVQgPSAxMDI0ICogMTAyNCAqIDEwMjQKdHJ5OgogICAgaW1wb3J0IHJlc291cmNlCiAgICByZXNvdXJjZS5zZXRybGltaXQocmVzb3VyY2UuUkxJTUlUX0FTLCAoTUVNX0xJTUlULCBNRU1fTElNSVQpKQpleGNlcHQgSW1wb3J0RXJyb3I6CiAgICBwYXNzCnRyeToKICAgIGZvciBfbSBpbiBsaXN0KHN5cy5tb2R1bGVzLmtleXMoKSk6CiAgICAgICAgaWYgX20gaW4gKCJjdHlwZXMiLCAiX2N0eXBlcyIsICJyZXNvdXJjZSIpIG9yIF9tLnN0YXJ0c3dpdGgoImN0eXBlcy4iKSBvciBfbS5zdGFydHN3aXRoKCJfY3R5cGVzLiIpOgogICAgICAgICAgICBkZWwgc3lzLm1vZHVsZXNbX21dCmV4Y2VwdCBFeGNlcHRpb246CiAgICBwYXNzCgpwYXlsb2FkID0ganNvbi5sb2FkcyhzeXMuc3RkaW4uYnVmZmVyLnJlYWQoKS5kZWNvZGUoInV0Zi04LXNpZyIpKQphcmdzICAgICAgPSBwYXlsb2FkLmdldCgiYXJncyIsIFtdKQpjb2RlICAgICAgPSBwYXlsb2FkLmdldCgiY29kZSIsICIiKQppc19hZG1pbiAgPSBib29sKHBheWxvYWQuZ2V0KCJhZG1pbiIsIEZhbHNlKSkKc3VzcGljaW9uID0gaW50KHBheWxvYWQuZ2V0KCJzdXNwaWNpb24iLCAwKSkKYXJnX2lkeCAgID0gMAoKaWYgY29kZS5zdHJpcCgpID09ICJfX3B5X2luZm9fbWFnaWNfN2YzYTliX18iOgogICAgcHJpbnQoanNvbi5kdW1wcyh7Im91dCI6IHN5cy52ZXJzaW9uLnNwbGl0KClbMF0sICJlcnIiOiAiIn0pKQogICAgc3lzLmV4aXQoMCkKCk1BWF9DT0RFX0xFTiA9IDIwMDAKaWYgbGVuKGNvZGUpID4gTUFYX0NPREVfTEVOOgogICAgcHJpbnQoanNvbi5kdW1wcyh7Im91dCI6ICIiLCAiZXJyIjogIkNvZGUgcXVhIGRhaSAodG9pIGRhICVkIGt5IHR1KSIgJSBNQVhfQ09ERV9MRU59KSkKICAgIHN5cy5leGl0KDApCgppZiBub3QgaXNfYWRtaW4gYW5kIHN1c3BpY2lvbiA+PSA3MDoKICAgIHByaW50KGpzb24uZHVtcHMoeyJvdXQiOiAiIiwgImVyciI6ICJDb2RlIGJpIGRhbmggZ2lhIHF1YSBraGEgbmdoaSAoJWQgZGllbSkuIENoaSBhZG1pbiBkdW9jIGNoYXkuIiAlIHN1c3BpY2lvbn0pKQogICAgc3lzLmV4aXQoMCkKCkFMTE9XRURfTU9EVUxFUyA9IHsibWF0aCIsInJhbmRvbSIsInN0cmluZyIsImRhdGV0aW1lIiwianNvbiIsInJlIiwiaXRlcnRvb2xzIiwiY29sbGVjdGlvbnMiLCJ0aW1lIn0KRk9SQklEREVOX05BTUVTID0gewogICAgIl9fYnVpbHRpbnNfXyIsIl9faW1wb3J0X18iLCJldmFsIiwiZXhlYyIsImNvbXBpbGUiLCJvcGVuIiwKICAgICJnbG9iYWxzIiwibG9jYWxzIiwidmFycyIsImRpciIsImdldGF0dHIiLCJzZXRhdHRyIiwiZGVsYXR0ciIsCiAgICAiYnJlYWtwb2ludCIsImV4aXQiLCJxdWl0IiwiaGVscCIsIm1lbW9yeXZpZXciLCJjbGFzc21ldGhvZCIsCiAgICAic3RhdGljbWV0aG9kIiwicHJvcGVydHkiLCJzdXBlciIsIm9iamVjdCIsInR5cGUiLAogICAgIl9fbG9hZGVyX18iLCJfX3NwZWNfXyIsIl9fcGFja2FnZV9fIiwiX19uYW1lX18iLCJfX2RvY19fIiwKICAgICJfX2ZpbGVfXyIsIl9fY2FjaGVkX18iLCJfX3BhdGhfXyIsIl9fY2xhc3NfXyIsIl9fZGljdF9fIiwiX19nbG9iYWxzX18iCn0KCmNsYXNzIFNlY3VyaXR5RXJyb3IoRXhjZXB0aW9uKToKICAgIHBhc3MKCmRlZiBfdmFsaWRhdGVfYXN0KHNvdXJjZSk6CiAgICB0cnk6CiAgICAgICAgX3RyZWUgPSBhc3QucGFyc2Uoc291cmNlKQogICAgZXhjZXB0IFN5bnRheEVycm9yIGFzIGU6CiAgICAgICAgcmV0dXJuIHN0cihlKQogICAgZm9yIF9ub2RlIGluIGFzdC53YWxrKF90cmVlKToKICAgICAgICAjIDEuIENo4bq3biBt4buNaSB0cnV5IGPhuq1wIHRodeG7mWMgdMOtbmggY2jhu6lhIF9fIGhv4bq3YyBmb3JtYXQKICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuQXR0cmlidXRlKToKICAgICAgICAgICAgaWYgX25vZGUuYXR0ci5zdGFydHN3aXRoKCJfXyIpIG9yIF9ub2RlLmF0dHIgaW4gKCJmb3JtYXQiLCAiZm9ybWF0X21hcCIsICJtcm8iLCAic3ViY2xhc3NlcyIpOgogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgdHJ1eSBjYXAgdGh1b2MgdGluaC9waHVvbmcgdGh1YyAnJXMnIiAlIF9ub2RlLmF0dHIKICAgICAgICAKICAgICAgICAjIDIuIENo4bq3biBn4buNaSBjw6FjIGjDoG0gdHJvbmcgZGFuaCBzw6FjaCDEkWVuCiAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0Lk5hbWUpIGFuZCBfbm9kZS5pZCBpbiBGT1JCSURERU5fTkFNRVM6CiAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBwaGVwIGR1bmcgJyVzJyIgJSBfbm9kZS5pZAogICAgICAgIAogICAgICAgICMgMy4gQ2jhurduIFN1YnNjcmlwdCB0cnV5IGPhuq1wIGtleSBi4bqvdCDEkeG6p3UgYuG6sW5nIF9fCiAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LlN1YnNjcmlwdCk6CiAgICAgICAgICAgIF9zbGljZSA9IF9ub2RlLnNsaWNlCiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX3NsaWNlLCBhc3QuSW5kZXgpOgogICAgICAgICAgICAgICAgX3NsaWNlID0gX3NsaWNlLnZhbHVlCiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX3NsaWNlLCBhc3QuQ29uc3RhbnQpIGFuZCBpc2luc3RhbmNlKF9zbGljZS52YWx1ZSwgc3RyKToKICAgICAgICAgICAgICAgIGlmIF9zbGljZS52YWx1ZS5zdGFydHN3aXRoKCJfXyIpIG9yIF9zbGljZS52YWx1ZSBpbiAoImZvcm1hdCIsICJmb3JtYXRfbWFwIik6CiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgdHJ1eSBjYXAga2V5IGNodW9pIG5ndXkgaGllbSIKICAgICAgICAKICAgICAgICAjIDQuIENo4bq3biDEkeG7i25oIG5naMSpYSBow6BtL2NsYXNzIGPDsyB0w6puIGLhuq90IMSR4bqndSBi4bqxbmcgX18KICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCAoYXN0LkZ1bmN0aW9uRGVmLCBhc3QuQXN5bmNGdW5jdGlvbkRlZiwgYXN0LkNsYXNzRGVmKSk6CiAgICAgICAgICAgIGlmIF9ub2RlLm5hbWUuc3RhcnRzd2l0aCgiX18iKToKICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBwaGVwIGRpbmggbmdoaWEgaGFtL2NsYXNzIGNvIHRlbiBiYXQgZGF1IGJhbmcgJ19fJyIKCiAgICAgICAgIyA1LiBDaOG6t24gSW1wb3J0IG5nb8OgaSB3aGl0ZWxpc3QKICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuSW1wb3J0KToKICAgICAgICAgICAgZm9yIF9hIGluIF9ub2RlLm5hbWVzOgogICAgICAgICAgICAgICAgaWYgX2EubmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6CiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgaW1wb3J0ICclcyciICUgX2EubmFtZQogICAgICAgIGlmIGlzaW5zdGFuY2UoX25vZGUsIGFzdC5JbXBvcnRGcm9tKToKICAgICAgICAgICAgaWYgX25vZGUubW9kdWxlIGFuZCBfbm9kZS5tb2R1bGUuc3BsaXQoIi4iKVswXSBub3QgaW4gQUxMT1dFRF9NT0RVTEVTOgogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIHBoZXAgaW1wb3J0ICclcyciICUgX25vZGUubW9kdWxlCiAgICByZXR1cm4gTm9uZQoKX2VyciA9IF92YWxpZGF0ZV9hc3QoY29kZSkKaWYgX2VycjoKICAgIHByaW50KGpzb24uZHVtcHMoeyJvdXQiOiAiIiwgImVyciI6IF9lcnJ9KSkKICAgIHN5cy5leGl0KDApCgpjb2RlX29iaiA9IGNvbXBpbGUoY29kZSwgIjxweT4iLCAiZXhlYyIpCmltcG9ydCBtYXRoLCByYW5kb20sIHN0cmluZywgZGF0ZXRpbWUsIGpzb24gYXMgX3B5anNvbiwgcmUsIGl0ZXJ0b29scywgY29sbGVjdGlvbnMsIHRpbWUKCmRlZiBfZmFrZV9pbnB1dChwcm9tcHQ9IiIpOgogICAgZ2xvYmFsIGFyZ19pZHgKICAgIGlmIGFyZ19pZHggPCBsZW4oYXJncyk6CiAgICAgICAgdiA9IGFyZ3NbYXJnX2lkeF07IGFyZ19pZHggKz0gMTsgcmV0dXJuIHYKICAgIHJhaXNlIEVPRkVycm9yKCJIZXQgZHUgbGlldSBkYXUgdmFvIChpbnB1dCkuIikKCmRlZiBfb3JpZ19pbXBvcnQobmFtZSwgKmEsICoqa3cpOgogICAgaWYgbmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6CiAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwIGltcG9ydCBtb2R1bGUgJyVzJyIgJSBuYW1lKQogICAgcmV0dXJuIGJ1aWx0aW5zLl9faW1wb3J0X18obmFtZSwgKmEsICoqa3cpCgpkZWYgX3NlY19vcGVuKCphLCAqKmt3KToKICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCB0aGFvIHRhYyB2b2kgZmlsZSIpCgpkZWYgX3NlY19mb3JtYXQoKmEsICoqa3cpOgogICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwIHN1IGR1bmcgcGh1b25nIHRodWMgLmZvcm1hdCgpIikKCl9SRU1PVkUgPSAoIm9wZW4iLCJleGVjIiwiZXZhbCIsImNvbXBpbGUiLCJfX2ltcG9ydF9fIiwKICAgICAgICAgICAiZ2V0YXR0ciIsInNldGF0dHIiLCJkZWxhdHRyIiwidmFycyIsImdsb2JhbHMiLCJsb2NhbHMiLCJkaXIiLAogICAgICAgICAgICJicmVha3BvaW50IiwibWVtb3J5dmlldyIsIm9iamVjdCIsInR5cGUiKQpzYWZlX2J1aWx0aW5zID0ge2s6IHYgZm9yIGssIHYgaW4gYnVpbHRpbnMuX19kaWN0X18uaXRlbXMoKSBpZiBrIG5vdCBpbiBfUkVNT1ZFfQpzYWZlX2J1aWx0aW5zLnVwZGF0ZSh7CiAgICAicHJpbnQiOiBwcmludCwgImlucHV0IjogX2Zha2VfaW5wdXQsICJfX2ltcG9ydF9fIjogX29yaWdfaW1wb3J0LAogICAgIm1hdGgiOiBtYXRoLCAicmFuZG9tIjogcmFuZG9tLCAic3RyaW5nIjogc3RyaW5nLCAiZGF0ZXRpbWUiOiBkYXRldGltZSwKICAgICJqc29uIjogX3B5anNvbiwgInJlIjogcmUsICJpdGVydG9vbHMiOiBpdGVydG9vbHMsCiAgICAiY29sbGVjdGlvbnMiOiBjb2xsZWN0aW9ucywgInRpbWUiOiB0aW1lLAogICAgIm9wZW4iOiBfc2VjX29wZW4sICJmb3JtYXQiOiBfc2VjX2Zvcm1hdCwKICAgICJUcnVlIjogVHJ1ZSwgIkZhbHNlIjogRmFsc2UsICJOb25lIjogTm9uZSwKICAgICJpbnQiOiBpbnQsICJmbG9hdCI6IGZsb2F0LCAic3RyIjogc3RyLCAiYm9vbCI6IGJvb2wsCiAgICAibGlzdCI6IGxpc3QsICJkaWN0IjogZGljdCwgInR1cGxlIjogdHVwbGUsICJzZXQiOiBzZXQsICJmcm96ZW5zZXQiOiBmcm96ZW5zZXQsCiAgICAiYnl0ZXMiOiBieXRlcywgImJ5dGVhcnJheSI6IGJ5dGVhcnJheSwKICAgICJyYW5nZSI6IHJhbmdlLCAibGVuIjogbGVuLCAibWluIjogbWluLCAibWF4IjogbWF4LCAic3VtIjogc3VtLCAiYWJzIjogYWJzLAogICAgInNvcnRlZCI6IHNvcnRlZCwgInJldmVyc2VkIjogcmV2ZXJzZWQsICJlbnVtZXJhdGUiOiBlbnVtZXJhdGUsICJ6aXAiOiB6aXAsCiAgICAibWFwIjogbWFwLCAiZmlsdGVyIjogZmlsdGVyLCAiYWxsIjogYWxsLCAiYW55IjogYW55LAogICAgImlzaW5zdGFuY2UiOiBpc2luc3RhbmNlLCAiaXNzdWJjbGFzcyI6IGlzc3ViY2xhc3MsICJjYWxsYWJsZSI6IGNhbGxhYmxlLAogICAgIml0ZXIiOiBpdGVyLCAibmV4dCI6IG5leHQsICJyZXByIjogcmVwciwgImhhc2giOiBoYXNoLCAiaWQiOiBpZCwKICAgICJjaHIiOiBjaHIsICJvcmQiOiBvcmQsICJoZXgiOiBoZXgsICJiaW4iOiBiaW4sICJvY3QiOiBvY3QsCiAgICAiZGl2bW9kIjogZGl2bW9kLCAicG93IjogcG93LCAicm91bmQiOiByb3VuZCwKICAgICJFeGNlcHRpb24iOiBFeGNlcHRpb24sICJWYWx1ZUVycm9yIjogVmFsdWVFcnJvciwgIlR5cGVFcnJvciI6IFR5cGVFcnJvciwKICAgICJLZXlFcnJvciI6IEtleUVycm9yLCAiSW5kZXhFcnJvciI6IEluZGV4RXJyb3IsICJSdW50aW1lRXJyb3IiOiBSdW50aW1lRXJyb3IsCiAgICAiWmVyb0RpdmlzaW9uRXJyb3IiOiBaZXJvRGl2aXNpb25FcnJvciwgIkF0dHJpYnV0ZUVycm9yIjogQXR0cmlidXRlRXJyb3IsCiAgICAiTmFtZUVycm9yIjogTmFtZUVycm9yLCAiSW1wb3J0RXJyb3IiOiBJbXBvcnRFcnJvciwgIlN0b3BJdGVyYXRpb24iOiBTdG9wSXRlcmF0aW9uLAogICAgIkFyaXRobWV0aWNFcnJvciI6IEFyaXRobWV0aWNFcnJvciwgIkxvb2t1cEVycm9yIjogTG9va3VwRXJyb3IsCiAgICAiTm90SW1wbGVtZW50ZWRFcnJvciI6IE5vdEltcGxlbWVudGVkRXJyb3IsICJQZXJtaXNzaW9uRXJyb3IiOiBQZXJtaXNzaW9uRXJyb3IsCiAgICAiRU9GRXJyb3IiOiBFT0ZFcnJvciwgIkZpbGVOb3RGb3VuZEVycm9yIjogRmlsZU5vdEZvdW5kRXJyb3IsCiAgICAiT1NFcnJvciI6IE9TRXJyb3IsICJJT0Vycm9yIjogSU9FcnJvciwKfSkKCkJMT0NLRURfUFJFRklYRVMgPSAoCiAgICAib3MuIiwgInN1YnByb2Nlc3MuIiwgInNvY2tldC4iLCAiY3R5cGVzLiIsICJzaHV0aWwuIiwgInRlbXBmaWxlLiIsCiAgICAid2lucmVnLiIsICJfd2luYXBpLiIsICJtbWFwLiIsICJzcWxpdGUzLiIsICJ1cmxsaWIuIiwgImh0dHAuIiwKICAgICJmdHBsaWIuIiwgInNtdHBsaWIuIiwgInRlbG5ldGxpYi4iLCAiaW1hcGxpYi4iLCAicG9wbGliLiIsICJubnRwbGliLiIsCiAgICAicGlja2xlLiIsICJtYXJzaGFsLiIsICJzaGVsdmUuIiwgImRibS4iLCAiemlwZmlsZS4iLCAidGFyZmlsZS4iLAogICAgImd6aXAuIiwgImJ6Mi4iLCAibHptYS4iLAopCl9ob29rX2VuYWJsZWQgPSBbRmFsc2VdCl9hbGxvd19leGVjID0gW0ZhbHNlXQoKZGVmIF9hdWRpdF9ob29rKGV2ZW50LCBldl9hcmdzKToKICAgIGlmIG5vdCBfaG9va19lbmFibGVkWzBdOgogICAgICAgIHJldHVybgogICAgaWYgZXZlbnQgPT0gImV4ZWMiIGFuZCBfYWxsb3dfZXhlY1swXToKICAgICAgICBfYWxsb3dfZXhlY1swXSA9IEZhbHNlCiAgICAgICAgcmV0dXJuCiAgICBpZiBldmVudCBpbiAoImV4ZWMiLCAiY29tcGlsZSIpOgogICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCAlcyIgJSBldmVudCkKICAgIGlmIGV2ZW50ID09ICJvcGVuIjoKICAgICAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBLaG9uZyBkdW9jIHBoZXAgbW8gZmlsZSIpCiAgICBpZiBldmVudCA9PSAiaW1wb3J0IjoKICAgICAgICBtb2QgPSAoZXZfYXJnc1swXSBvciAiIikuc3BsaXQoIi4iKVswXSBpZiBldl9hcmdzIGVsc2UgIiIKICAgICAgICBpZiBtb2Qgbm90IGluIEFMTE9XRURfTU9EVUxFUzoKICAgICAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBwaGVwIGltcG9ydCAnJXMnIiAlIG1vZCkKICAgICAgICByZXR1cm4KICAgIGlmIGV2ZW50IGluICgic3lzLmFkZGF1ZGl0aG9vayIsICJzeXMuc2V0dHJhY2UiLCAic3lzLnNldHByb2ZpbGUiKToKICAgICAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBLaG9uZyBkdW9jIHBoZXAgJXMiICUgZXZlbnQpCiAgICBpZiBldmVudC5zdGFydHN3aXRoKEJMT0NLRURfUFJFRklYRVMpOgogICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgcGhlcCAlcyIgJSBldmVudCkKCnN5cy5hZGRhdWRpdGhvb2soX2F1ZGl0X2hvb2spCnN0ZG91dF9jYXB0dXJlID0gaW8uU3RyaW5nSU8oKQpzdGRlcnJfY2FwdHVyZSA9IGlvLlN0cmluZ0lPKCkKc3lzLnN0ZG91dCA9IHN0ZG91dF9jYXB0dXJlCnN5cy5zdGRlcnIgPSBzdGRlcnJfY2FwdHVyZQoKX2hvb2tfZW5hYmxlZFswXSA9IFRydWUKX2FsbG93X2V4ZWNbMF0gPSBUcnVlCnRyeToKICAgIF9nID0geyJfX2J1aWx0aW5zX18iOiBzYWZlX2J1aWx0aW5zLCAiX19uYW1lX18iOiAiX19tYWluX18ifQogICAgdHJ5OgogICAgICAgIGV4ZWMoY29kZV9vYmosIF9nKQogICAgZmluYWxseToKICAgICAgICBfYWxsb3dfZXhlY1swXSA9IEZhbHNlCmV4Y2VwdCBTZWN1cml0eUVycm9yIGFzIGU6CiAgICBzeXMuc3RkZXJyLndyaXRlKHN0cihlKSkKZXhjZXB0IE1lbW9yeUVycm9yOgogICAgc3lzLnN0ZGVyci53cml0ZSgiVnVvdCBxdWEgZ2lvaSBoYW4gYm8gbmhvIDFHQiIpCmV4Y2VwdCBCYXNlRXhjZXB0aW9uIGFzIGU6CiAgICBfaG9va19lbmFibGVkWzBdID0gRmFsc2UKICAgIF9hbGxvd19leGVjWzBdID0gRmFsc2UKICAgIHRyeToKICAgICAgICBtc2cgPSAiJXM6ICVzIiAlICh0eXBlKGUpLl9fbmFtZV9fLCBzdHIoZSkgaWYgc3RyKGUpIGVsc2UgIihubyBtZXNzYWdlKSIpCiAgICAgICAgc3lzLnN0ZGVyci53cml0ZShtc2cpCiAgICBleGNlcHQgRXhjZXB0aW9uOgogICAgICAgIHN5cy5zdGRlcnIud3JpdGUodHlwZShlKS5fX25hbWVfXykKZmluYWxseToKICAgIF9ob29rX2VuYWJsZWRbMF0gPSBGYWxzZQoKX291dCA9IHN0ZG91dF9jYXB0dXJlLmdldHZhbHVlKCkKX2VyciA9IHN0ZGVycl9jYXB0dXJlLmdldHZhbHVlKCkKc3lzLnN0ZG91dCA9IHN5cy5fX3N0ZG91dF9fCnN5cy5zdGRlcnIgPSBzeXMuX19zdGRlcnJfXwpwcmludChqc29uLmR1bXBzKHsib3V0IjogX291dCwgImVyciI6IF9lcnJ9KSk=", "base64").toString("utf8");

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
