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

        const wrapper = Buffer.from("aW1wb3J0IHN5cywganNvbiwgaW8sIGJ1aWx0aW5zLCB0cmFjZWJhY2ssIGFzdAoKdHJ5OgogICAgTUVNX0xJTUlUID0gMTAyNCAqIDEwMjQgKiAxMDI0CiAgICB0cnk6CiAgICAgICAgaW1wb3J0IHJlc291cmNlCiAgICAgICAgcmVzb3VyY2Uuc2V0cmxpbWl0KHJlc291cmNlLlJMSU1JVF9BUywgKE1FTV9MSU1JVCwgTUVNX0xJTUlUKSkKICAgIGV4Y2VwdCBJbXBvcnRFcnJvcjogcGFzcwogICAgCiAgICB0cnk6CiAgICAgICAgaW1wb3J0IGN0eXBlcwogICAgICAgIGZyb20gY3R5cGVzIGltcG9ydCB3aW50eXBlcyBhcyBfVwogICAgICAgIF9rID0gY3R5cGVzLldpbkRMTCgia2VybmVsMzIiLCB1c2VfbGFzdF9lcnJvcj1UcnVlKQogICAgICAgIF9rLkNyZWF0ZUpvYk9iamVjdFcucmVzdHlwZSA9IF9XLkhBTkRMRQogICAgICAgIF9rLkNyZWF0ZUpvYk9iamVjdFcuYXJndHlwZXMgPSBbY3R5cGVzLmNfdm9pZF9wLCBfVy5MUENXU1RSXQogICAgICAgIF9rLlNldEluZm9ybWF0aW9uSm9iT2JqZWN0LnJlc3R5cGUgPSBfVy5CT09MCiAgICAgICAgX2suU2V0SW5mb3JtYXRpb25Kb2JPYmplY3QuYXJndHlwZXMgPSBbX1cuSEFORExFLCBjdHlwZXMuY19pbnQsIGN0eXBlcy5jX3ZvaWRfcCwgX1cuRFdPUkRdCiAgICAgICAgX2suQXNzaWduUHJvY2Vzc1RvSm9iT2JqZWN0LnJlc3R5cGUgPSBfVy5CT09MCiAgICAgICAgX2suQXNzaWduUHJvY2Vzc1RvSm9iT2JqZWN0LmFyZ3R5cGVzID0gW19XLkhBTkRMRSwgX1cuSEFORExFXQogICAgICAgIF9rLkdldEN1cnJlbnRQcm9jZXNzLnJlc3R5cGUgPSBfVy5IQU5ETEUKICAgICAgICBjbGFzcyBfSkIoY3R5cGVzLlN0cnVjdHVyZSk6CiAgICAgICAgICAgIF9maWVsZHNfID0gWygiUGVyUHJvY2Vzc1VzZXJUaW1lTGltaXQiLCBjdHlwZXMuY19pbnQ2NCksICgiUGVySm9iVXNlclRpbWVMaW1pdCIsIGN0eXBlcy5jX2ludDY0KSwgKCJMaW1pdEZsYWdzIiwgX1cuRFdPUkQpLCAoIk1pbmltdW1Xb3JraW5nU2V0U2l6ZSIsIGN0eXBlcy5jX3NpemVfdCksICgiTWF4aW11bVdvcmtpbmdTZXRTaXplIiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJBY3RpdmVQcm9jZXNzTGltaXQiLCBfVy5EV09SRCksICgiQWZmaW5pdHkiLCBjdHlwZXMuY19zaXplX3QpLCAoIlByaW9yaXR5Q2xhc3MiLCBfVy5EV09SRCksICgiU2NoZWR1bGluZ0NsYXNzIiwgX1cuRFdPUkQpXQogICAgICAgIGNsYXNzIF9JTyhjdHlwZXMuU3RydWN0dXJlKToKICAgICAgICAgICAgX2ZpZWxkc18gPSBbKCJSZWFkT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIldyaXRlT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIk90aGVyT3BlcmF0aW9uQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIlJlYWRUcmFuc2ZlckNvdW50IiwgY3R5cGVzLmNfdWludDY0KSwgKCJXcml0ZVRyYW5zZmVyQ291bnQiLCBjdHlwZXMuY191aW50NjQpLCAoIk90aGVyVHJhbnNmZXJDb3VudCIsIGN0eXBlcy5jX3VpbnQ2NCldCiAgICAgICAgY2xhc3MgX0VYVChjdHlwZXMuU3RydWN0dXJlKToKICAgICAgICAgICAgX2ZpZWxkc18gPSBbKCJCYXNpY0xpbWl0SW5mb3JtYXRpb24iLCBfSkIpLCAoIklvSW5mbyIsIF9JTyksICgiUHJvY2Vzc01lbW9yeUxpbWl0IiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJKb2JNZW1vcnlMaW1pdCIsIGN0eXBlcy5jX3NpemVfdCksICgiUGVha1Byb2Nlc3NNZW1vcnlVc2VkIiwgY3R5cGVzLmNfc2l6ZV90KSwgKCJQZWFrSm9iTWVtb3J5VXNlZCIsIGN0eXBlcy5jX3NpemVfdCldCiAgICAgICAgX2pvYiA9IF9rLkNyZWF0ZUpvYk9iamVjdFcoTm9uZSwgTm9uZSkKICAgICAgICBpZiBfam9iOgogICAgICAgICAgICBfaW5mbyA9IF9FWFQoKQogICAgICAgICAgICBfaW5mby5CYXNpY0xpbWl0SW5mb3JtYXRpb24uTGltaXRGbGFncyA9IDB4MiB8IDB4OCB8IDB4MTAwIHwgMHgyMDAwCiAgICAgICAgICAgIF9pbmZvLkJhc2ljTGltaXRJbmZvcm1hdGlvbi5QZXJQcm9jZXNzVXNlclRpbWVMaW1pdCA9IDUgKiAxMDAwMDAwMCAKICAgICAgICAgICAgX2luZm8uQmFzaWNMaW1pdEluZm9ybWF0aW9uLkFjdGl2ZVByb2Nlc3NMaW1pdCA9IDEKICAgICAgICAgICAgX2luZm8uUHJvY2Vzc01lbW9yeUxpbWl0ID0gTUVNX0xJTUlUCiAgICAgICAgICAgIF9rLlNldEluZm9ybWF0aW9uSm9iT2JqZWN0KF9qb2IsIDksIGN0eXBlcy5ieXJlZihfaW5mbyksIGN0eXBlcy5zaXplb2YoX2luZm8pKQogICAgICAgICAgICBfay5Bc3NpZ25Qcm9jZXNzVG9Kb2JPYmplY3QoX2pvYiwgX2suR2V0Q3VycmVudFByb2Nlc3MoKSkKICAgIGV4Y2VwdCBFeGNlcHRpb246IHBhc3MKICAgIHRyeToKICAgICAgICBmb3IgX20gaW4gbGlzdChzeXMubW9kdWxlcy5rZXlzKCkpOgogICAgICAgICAgICBpZiBfbSBpbiAoImN0eXBlcyIsICJfY3R5cGVzIiwgInJlc291cmNlIikgb3IgX20uc3RhcnRzd2l0aCgiY3R5cGVzLiIpIG9yIF9tLnN0YXJ0c3dpdGgoIl9jdHlwZXMuIik6CiAgICAgICAgICAgICAgICBkZWwgc3lzLm1vZHVsZXNbX21dCiAgICBleGNlcHQgRXhjZXB0aW9uOiBwYXNzCgogICAgcGF5bG9hZCA9IGpzb24ubG9hZHMoc3lzLnN0ZGluLmJ1ZmZlci5yZWFkKCkuZGVjb2RlKCJ1dGYtOC1zaWciKSkKICAgIGFyZ3MsIGNvZGUsIGlzX2FkbWluLCBzdXNwaWNpb24gPSBwYXlsb2FkLmdldCgiYXJncyIsIFtdKSwgcGF5bG9hZC5nZXQoImNvZGUiLCAiIiksIGJvb2wocGF5bG9hZC5nZXQoImFkbWluIiwgRmFsc2UpKSwgaW50KHBheWxvYWQuZ2V0KCJzdXNwaWNpb24iLCAwKSkKICAgIGFyZ19pZHggPSAwCgogICAgaWYgY29kZS5zdHJpcCgpID09ICJfX3B5X2luZm9fbWFnaWNfN2YzYTliX18iOgogICAgICAgIHN5cy5zdGRvdXQuYnVmZmVyLndyaXRlKGpzb24uZHVtcHMoeyJvdXQiOiBzeXMudmVyc2lvbi5zcGxpdCgpWzBdLCAiZXJyIjogIiJ9KS5lbmNvZGUoJ3V0Zi04JykgKyBiIlxuIik7IHN5cy5zdGRvdXQuYnVmZmVyLmZsdXNoKCk7IHN5cy5leGl0KDApCgogICAgaWYgbGVuKGNvZGUpID4gMjAwMCBvciAobm90IGlzX2FkbWluIGFuZCBzdXNwaWNpb24gPj0gNzApOgogICAgICAgIHN5cy5zdGRvdXQuYnVmZmVyLndyaXRlKGpzb24uZHVtcHMoeyJvdXQiOiAiIiwgImVyciI6ICJDb2RlIGJpIGNoYW4ifSkuZW5jb2RlKCd1dGYtOCcpICsgYiJcbiIpOyBzeXMuc3Rkb3V0LmJ1ZmZlci5mbHVzaCgpOyBzeXMuZXhpdCgwKQoKICAgIEFMTE9XRURfTU9EVUxFUyA9IHsibWF0aCIsInJhbmRvbSIsInN0cmluZyIsImRhdGV0aW1lIiwianNvbiIsInJlIiwiaXRlcnRvb2xzIiwiY29sbGVjdGlvbnMiLCJ0aW1lIn0KICAgIEZPUkJJRERFTl9OQU1FUyA9IHsiX19idWlsdGluc19fIiwiX19pbXBvcnRfXyIsImV2YWwiLCJleGVjIiwiY29tcGlsZSIsIm9wZW4iLCJnbG9iYWxzIiwibG9jYWxzIiwidmFycyIsImRpciIsImdldGF0dHIiLCJzZXRhdHRyIiwiZGVsYXR0ciIsImJyZWFrcG9pbnQiLCJleGl0IiwicXVpdCIsImhlbHAiLCJtZW1vcnl2aWV3IiwiY2xhc3NtZXRob2QiLCJzdGF0aWNtZXRob2QiLCJwcm9wZXJ0eSIsInN1cGVyIiwib2JqZWN0IiwidHlwZSIsIl9fbG9hZGVyX18iLCJfX3NwZWNfXyIsIl9fcGFja2FnZV9fIiwiX19uYW1lX18iLCJfX2RvY19fIiwiX19maWxlX18iLCJfX2NhY2hlZF9fIiwiX19wYXRoX18iLCJfX2NsYXNzX18iLCJfX2RpY3RfXyIsIl9fZ2xvYmFsc19fIiwiZl9nbG9iYWxzIiwgImZfbG9jYWxzIiwgImZfYnVpbHRpbnMiLCAiZ2lfZnJhbWUiLCAiY3JfZnJhbWUiLCAiYWdfZnJhbWUifQogICAgREFOR0VST1VTX0FUVFJTID0geyJmb3JtYXQiLCAiZm9ybWF0X21hcCIsICJtcm8iLCAic3ViY2xhc3NlcyIsICJnaV9mcmFtZSIsICJjcl9mcmFtZSIsICJhZ19mcmFtZSIsICJnaV9ydW5uaW5nIiwgImNyX2F3YWl0IiwgImFnX2F3YWl0IiwgInNlbmQiLCAidGhyb3ciLCAiY2xvc2UiLCAidGJfZnJhbWUiLCAidGJfbmV4dCIsICJ0Yl9saW5lbm8iLCAiZl9iYWNrIiwgImZfY29kZSIsICJmX2dsb2JhbHMiLCAiZl9sb2NhbHMiLCAiZl9idWlsdGlucyIsICJmX3RyYWNlIiwgImNvX2NvbnN0cyIsICJjb19uYW1lcyIsICJjb192YXJuYW1lcyIsICJjb19jb2RlIiwgImNvX2ZyZWV2YXJzIiwgImNvX2NlbGx2YXJzIn0KCiAgICBkZWYgX3ZhbGlkYXRlX2FzdChzb3VyY2UpOgogICAgICAgIHRyeTogX3RyZWUgPSBhc3QucGFyc2Uoc291cmNlKQogICAgICAgIGV4Y2VwdCBTeW50YXhFcnJvciBhcyBlOiByZXR1cm4gc3RyKGUpCiAgICAgICAgCiAgICAgICAgZm9yIF9ub2RlIGluIGFzdC53YWxrKF90cmVlKToKICAgICAgICAgICAgIyAxLiBLaWVtIHRyYSBBdHRyaWJ1dGUKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgYXN0LkF0dHJpYnV0ZSk6CiAgICAgICAgICAgICAgICBpZiBfbm9kZS5hdHRyLnN0YXJ0c3dpdGgoIl9fIikgb3IgX25vZGUuYXR0ciBpbiBEQU5HRVJPVVNfQVRUUlM6IHJldHVybiAiS2hvbmcgZHVvYyB0cnV5IGNhcCB0aHVvYyB0aW5oIgogICAgICAgICAgICAjIDIuIEtpZW0gdHJhIFRlbiBiaWVuL2hhbQogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuTmFtZSkgYW5kIF9ub2RlLmlkIGluIEZPUkJJRERFTl9OQU1FUzogCiAgICAgICAgICAgICAgICByZXR1cm4gIktob25nIGR1b2MgZHVuZyAnJXMnIiAlIF9ub2RlLmlkCiAgICAgICAgICAgICMgMy4gS2llbSB0cmEgU3Vic2NyaXB0IChvYmpbJ19fY2xhc3NfXyddKQogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuU3Vic2NyaXB0KToKICAgICAgICAgICAgICAgIF9zbGljZSA9IF9ub2RlLnNsaWNlCiAgICAgICAgICAgICAgICBpZiBoYXNhdHRyKGFzdCwgIkluZGV4IikgYW5kIGlzaW5zdGFuY2UoX3NsaWNlLCBhc3QuSW5kZXgpOiBfc2xpY2UgPSBfc2xpY2UudmFsdWUKICAgICAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX3NsaWNlLCBhc3QuQ29uc3RhbnQpIGFuZCBpc2luc3RhbmNlKF9zbGljZS52YWx1ZSwgc3RyKToKICAgICAgICAgICAgICAgICAgICBpZiBfc2xpY2UudmFsdWUuc3RhcnRzd2l0aCgiX18iKSBvciBfc2xpY2UudmFsdWUgaW4gREFOR0VST1VTX0FUVFJTOiByZXR1cm4gIktob25nIGR1b2MgdHJ1eSBjYXAga2V5IgogICAgICAgICAgICAjIDQuIEtpZW0gdHJhIERpbmggbmdoaWEgSGFtL0NsYXNzIChDaGFuIHRlbiBfXykKICAgICAgICAgICAgaWYgaXNpbnN0YW5jZShfbm9kZSwgKGFzdC5GdW5jdGlvbkRlZiwgYXN0LkFzeW5jRnVuY3Rpb25EZWYsIGFzdC5DbGFzc0RlZikpOgogICAgICAgICAgICAgICAgaWYgX25vZGUubmFtZS5zdGFydHN3aXRoKCJfXyIpOiByZXR1cm4gIktob25nIGR1b2MgZGluaCBuZ2hpYSBoYW0gX18iCiAgICAgICAgICAgICMgNS4gS2llbSB0cmEgSW1wb3J0CiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2UoX25vZGUsIGFzdC5JbXBvcnQpOgogICAgICAgICAgICAgICAgZm9yIF9hIGluIF9ub2RlLm5hbWVzOgogICAgICAgICAgICAgICAgICAgIGlmIF9hLm5hbWUuc3BsaXQoIi4iKVswXSBub3QgaW4gQUxMT1dFRF9NT0RVTEVTOiByZXR1cm4gIktob25nIGR1b2MgaW1wb3J0IgogICAgICAgICAgICBpZiBpc2luc3RhbmNlKF9ub2RlLCBhc3QuSW1wb3J0RnJvbSk6CiAgICAgICAgICAgICAgICBpZiBfbm9kZS5tb2R1bGUgYW5kIF9ub2RlLm1vZHVsZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6IHJldHVybiAiS2hvbmcgZHVvYyBpbXBvcnQiCiAgICAgICAgcmV0dXJuIE5vbmUKCiAgICBfZXJyID0gX3ZhbGlkYXRlX2FzdChjb2RlKQogICAgaWYgX2VycjoKICAgICAgICBzeXMuc3Rkb3V0LmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0IjogIiIsICJlcnIiOiBfZXJyfSkuZW5jb2RlKCd1dGYtOCcpICsgYiJcbiIpOyBzeXMuc3Rkb3V0LmJ1ZmZlci5mbHVzaCgpOyBzeXMuZXhpdCgwKQoKICAgIGNvZGVfb2JqID0gY29tcGlsZShjb2RlLCAiPHB5PiIsICJleGVjIikKICAgIGltcG9ydCBtYXRoLCByYW5kb20sIHN0cmluZywgZGF0ZXRpbWUsIGpzb24gYXMgX3B5anNvbiwgcmUsIGl0ZXJ0b29scywgY29sbGVjdGlvbnMsIHRpbWUKICAgIGRlZiBfZmFrZV9pbnB1dChwcm9tcHQ9IiIpOgogICAgICAgIGdsb2JhbCBhcmdfaWR4CiAgICAgICAgaWYgYXJnX2lkeCA8IGxlbihhcmdzKTogdiA9IGFyZ3NbYXJnX2lkeF07IGFyZ19pZHggKz0gMTsgcmV0dXJuIHYKICAgICAgICByYWlzZSBFT0ZFcnJvcigiSGV0IGR1IGxpZXUiKQogICAgZGVmIF9vcmlnX2ltcG9ydChuYW1lLCAqYSwgKiprdyk6CiAgICAgICAgaWYgbmFtZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgICAgIHJldHVybiBidWlsdGlucy5fX2ltcG9ydF9fKG5hbWUsICphLCAqKmt3KQogICAgZGVmIF9zZWNfb3BlbigqYSwgKiprdyk6IHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQiKQogICAgZGVmIF9zZWNfZm9ybWF0KCphLCAqKmt3KTogcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdCIpCiAgICBjbGFzcyBTZWN1cml0eUVycm9yKEV4Y2VwdGlvbik6IHBhc3MKICAgIAogICAgX1JFTU9WRSA9ICgib3BlbiIsImV4ZWMiLCJldmFsIiwiY29tcGlsZSIsIl9faW1wb3J0X18iLCJnZXRhdHRyIiwic2V0YXR0ciIsImRlbGF0dHIiLCJ2YXJzIiwiZ2xvYmFscyIsImxvY2FscyIsImRpciIsImJyZWFrcG9pbnQiLCJtZW1vcnl2aWV3Iiwib2JqZWN0IiwidHlwZSIpCiAgICBzYWZlX2J1aWx0aW5zID0ge2s6IHYgZm9yIGssIHYgaW4gYnVpbHRpbnMuX19kaWN0X18uaXRlbXMoKSBpZiBrIG5vdCBpbiBfUkVNT1ZFfQogICAgc2FmZV9idWlsdGlucy51cGRhdGUoeyJwcmludCI6IHByaW50LCAiaW5wdXQiOiBfZmFrZV9pbnB1dCwgIl9faW1wb3J0X18iOiBfb3JpZ19pbXBvcnQsICJtYXRoIjogbWF0aCwgInJhbmRvbSI6IHJhbmRvbSwgInN0cmluZyI6IHN0cmluZywgImRhdGV0aW1lIjogZGF0ZXRpbWUsICJqc29uIjogX3B5anNvbiwgInJlIjogcmUsICJpdGVydG9vbHMiOiBpdGVydG9vbHMsICJjb2xsZWN0aW9ucyI6IGNvbGxlY3Rpb25zLCAidGltZSI6IHRpbWUsICJvcGVuIjogX3NlY19vcGVuLCAiZm9ybWF0IjogX3NlY19mb3JtYXQsICJUcnVlIjogVHJ1ZSwgIkZhbHNlIjogRmFsc2UsICJOb25lIjogTm9uZSwgImludCI6IGludCwgImZsb2F0IjogZmxvYXQsICJzdHIiOiBzdHIsICJib29sIjogYm9vbCwgImxpc3QiOiBsaXN0LCAiZGljdCI6IGRpY3QsICJ0dXBsZSI6IHR1cGxlLCAic2V0Ijogc2V0LCAiZnJvemVuc2V0IjogZnJvemVuc2V0LCAiYnl0ZXMiOiBieXRlcywgImJ5dGVhcnJheSI6IGJ5dGVhcnJheSwgInJhbmdlIjogcmFuZ2UsICJsZW4iOiBsZW4sICJtaW4iOiBtaW4sICJtYXgiOiBtYXgsICJzdW0iOiBzdW0sICJhYnMiOiBhYnMsICJzb3J0ZWQiOiBzb3J0ZWQsICJyZXZlcnNlZCI6IHJldmVyc2VkLCAiZW51bWVyYXRlIjogZW51bWVyYXRlLCAiemlwIjogemlwLCAibWFwIjogbWFwLCAiZmlsdGVyIjogZmlsdGVyLCAiYWxsIjogYWxsLCAiYW55IjogYW55LCAiaXNpbnN0YW5jZSI6IGlzaW5zdGFuY2UsICJpc3N1YmNsYXNzIjogaXNzdWJjbGFzcywgImNhbGxhYmxlIjogY2FsbGFibGUsICJpdGVyIjogaXRlciwgIm5leHQiOiBuZXh0LCAicmVwciI6IHJlcHIsICJoYXNoIjogaGFzaCwgImlkIjogaWQsICJjaHIiOiBjaHIsICJvcmQiOiBvcmQsICJoZXgiOiBoZXgsICJiaW4iOiBiaW4sICJvY3QiOiBvY3QsICJkaXZtb2QiOiBkaXZtb2QsICJwb3ciOiBwb3csICJyb3VuZCI6IHJvdW5kLCAiRXhjZXB0aW9uIjogRXhjZXB0aW9uLCAiVmFsdWVFcnJvciI6IFZhbHVlRXJyb3IsICJUeXBlRXJyb3IiOiBUeXBlRXJyb3IsICJLZXlFcnJvciI6IEtleUVycm9yLCAiSW5kZXhFcnJvciI6IEluZGV4RXJyb3IsICJSdW50aW1lRXJyb3IiOiBSdW50aW1lRXJyb3IsICJaZXJvRGl2aXNpb25FcnJvciI6IFplcm9EaXZpc2lvbkVycm9yLCAiQXR0cmlidXRlRXJyb3IiOiBBdHRyaWJ1dGVFcnJvciwgIk5hbWVFcnJvciI6IE5hbWVFcnJvciwgIkltcG9ydEVycm9yIjogSW1wb3J0RXJyb3IsICJTdG9wSXRlcmF0aW9uIjogU3RvcEl0ZXJhdGlvbiwgIkFyaXRobWV0aWNFcnJvciI6IEFyaXRobWV0aWNFcnJvciwgIkxvb2t1cEVycm9yIjogTG9va3VwRXJyb3IsICJOb3RJbXBsZW1lbnRlZEVycm9yIjogTm90SW1wbGVtZW50ZWRFcnJvciwgIlBlcm1pc3Npb25FcnJvciI6IFBlcm1pc3Npb25FcnJvciwgIkVPRkVycm9yIjogRU9GRXJyb3IsICJGaWxlTm90Rm91bmRFcnJvciI6IEZpbGVOb3RGb3VuZEVycm9yLCAiT1NFcnJvciI6IE9TRXJyb3IsICJJT0Vycm9yIjogSU9FcnJvcn0pCiAgICAKICAgIF9ob29rX2VuYWJsZWQsIF9hbGxvd19leGVjID0gW0ZhbHNlXSwgW0ZhbHNlXQogICAgZGVmIF9hdWRpdF9ob29rKGV2ZW50LCBldl9hcmdzKToKICAgICAgICBpZiBub3QgX2hvb2tfZW5hYmxlZFswXTogcmV0dXJuCiAgICAgICAgaWYgZXZlbnQgPT0gImV4ZWMiIGFuZCBfYWxsb3dfZXhlY1swXTogX2FsbG93X2V4ZWNbMF0gPSBGYWxzZTsgcmV0dXJuCiAgICAgICAgaWYgZXZlbnQgaW4gKCJleGVjIiwgImNvbXBpbGUiLCAib3BlbiIpOiByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0IikKICAgICAgICBpZiBldmVudCA9PSAiaW1wb3J0IjoKICAgICAgICAgICAgbW9kID0gKGV2X2FyZ3NbMF0gb3IgIiIpLnNwbGl0KCIuIilbMF0gaWYgZXZfYXJncyBlbHNlICIiCiAgICAgICAgICAgIGlmIG1vZCBub3QgaW4gQUxMT1dFRF9NT0RVTEVTOiByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0IikKICAgIHN5cy5hZGRhdWRpdGhvb2soX2F1ZGl0X2hvb2spCiAgICAKICAgIHN0ZG91dF9jYXB0dXJlLCBzdGRlcnJfY2FwdHVyZSA9IGlvLlN0cmluZ0lPKCksIGlvLlN0cmluZ0lPKCkKICAgIHN5cy5zdGRvdXQsIHN5cy5zdGRlcnIgPSBzdGRvdXRfY2FwdHVyZSwgc3RkZXJyX2NhcHR1cmUKCiAgICBfaG9va19lbmFibGVkWzBdLCBfYWxsb3dfZXhlY1swXSA9IFRydWUsIFRydWUKICAgIHRyeToKICAgICAgICBfZyA9IHsiX19idWlsdGluc19fIjogc2FmZV9idWlsdGlucywgIl9fbmFtZV9fIjogIl9fbWFpbl9fIn0KICAgICAgICB0cnk6IGV4ZWMoY29kZV9vYmosIF9nKQogICAgICAgIGZpbmFsbHk6IF9hbGxvd19leGVjWzBdID0gRmFsc2UKICAgIGV4Y2VwdCBTZWN1cml0eUVycm9yIGFzIGU6IHN5cy5zdGRlcnIud3JpdGUoc3RyKGUpKQogICAgZXhjZXB0IE1lbW9yeUVycm9yOiBzeXMuc3RkZXJyLndyaXRlKCJWdW90IHF1YSBibyBuaG8gMUdCIikKICAgIGV4Y2VwdCBCYXNlRXhjZXB0aW9uIGFzIGU6CiAgICAgICAgX2hvb2tfZW5hYmxlZFswXSwgX2FsbG93X2V4ZWNbMF0gPSBGYWxzZSwgRmFsc2UKICAgICAgICB0cnk6IHN5cy5zdGRlcnIud3JpdGUoIiVzOiAlcyIgJSAodHlwZShlKS5fX25hbWVfXywgc3RyKGUpIGlmIHN0cihlKSBlbHNlICIiKSkKICAgICAgICBleGNlcHQ6IHN5cy5zdGRlcnIud3JpdGUodHlwZShlKS5fX25hbWVfXykKICAgIGZpbmFsbHk6IF9ob29rX2VuYWJsZWRbMF0gPSBGYWxzZQoKICAgIF9vdXQsIF9lcnIgPSBzdGRvdXRfY2FwdHVyZS5nZXR2YWx1ZSgpLCBzdGRlcnJfY2FwdHVyZS5nZXR2YWx1ZSgpCiAgICBpZiBsZW4oX291dCkgPiA4MDA6IF9vdXQgPSBfb3V0Wzo4MDBdICsgIlxuLi4uKGRhIGNhdCBib3Qgb3V0cHV0KSIKICAgICAgICAKICAgIF9yZXNfYnl0ZXMgPSBqc29uLmR1bXBzKHsib3V0IjogX291dCwgImVyciI6IF9lcnJ9KS5lbmNvZGUoJ3V0Zi04JykKICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShfcmVzX2J5dGVzICsgYiJcbiIpOyBzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKQoKZXhjZXB0IEJhc2VFeGNlcHRpb24gYXMgZ2xvYmFsX2VycjoKICAgIHRyeToKICAgICAgICBtc2cgPSAiTG9pIFNhbmRib3g6ICVzOiAlcyIgJSAodHlwZShnbG9iYWxfZXJyKS5fX25hbWVfXywgc3RyKGdsb2JhbF9lcnIpKQogICAgICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0IjogIiIsICJlcnIiOiBtc2d9KS5lbmNvZGUoJ3V0Zi04JykgKyBiIlxuIik7IHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci5mbHVzaCgpCiAgICBleGNlcHQ6IHBhc3M=", "base64").toString("utf8");

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
