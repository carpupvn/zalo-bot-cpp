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

    log("QR login thành công");

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

  throw new Error("Tất cả các lần QR login đều thất bại");

}



const creds = await getCredentials();

const _credsArr = creds && Array.isArray(creds.cookie) ? creds.cookie : [];

log("Số cookie đọc được: " + _credsArr.length + ", imei=" + (creds && creds.imei ? "YES" : "NO"));



let api = null;



if (_credsArr.length >= 2 && creds && creds.imei) {

  try {

    log("Đang đăng nhập zca-js (cookie)...");

    const zalo = new Zalo({ selfListen: true, imageMetadataGetter });

    api = await zalo.login({

      cookie: _credsArr,

      imei: creds.imei,

      userAgent: creds.user_agent,

    });

    log("Đăng nhập thành công (cookie)");

  } catch (e) {

    log("Cookie login thất bại: " + (e && e.message ? e.message : String(e)));

    api = null;

  }

} else {

  log("Không đủ cookie hoặc thiếu imei, bỏ qua cookie login");

}



if (!api) {

  log("Chuyển sang QR login...");

  try {

    api = await tryQRLogin(creds || {});

    log("Đăng nhập thành công (QR)");

    saveCookieToConfig(api, creds || {});

  } catch (e2) {

    log("QR login thất bại: " + (e2 && e2.message ? e2.message : String(e2)));

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




function handleGroupEvent(event) {
  try {
    const type = String(event?.type || "");
    const threadId = String(event?.threadId || event?.data?.groupId || "");
    const d = event?.data || {};
    const groupName = String(d.groupName || "");
    const sourceId = String(d.sourceId || "");
    const members = Array.isArray(d.updateMembers)
      ? d.updateMembers.map(m => ({
          id: String(m?.id || ""),
          dName: String(m?.dName || ""),
          avatar: String(m?.avatar || m?.avatar_25 || "")
        }))
      : [];
    emit({
      event: "group_event",
      data: { type, threadId, groupName, sourceId, updateMembers: members }
    });
  } catch (e) {
    log("handleGroupEvent error: " + (e?.message || String(e)));
  }
}

api.listener.on("undo", handleUndoEvent);
api.listener.on("group_event", handleGroupEvent);



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
      api.listener.on("group_event", handleGroupEvent);

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
      try {
        const sid = Number(cmd.sticker_id);
        let r;
        if (!Number.isNaN(sid) && sid > 0 && typeof api.getStickersDetail === "function" && typeof api.sendSticker === "function") {
          const details = await api.getStickersDetail(sid);
          const st = Array.isArray(details) && details[0] ? details[0] : null;
          if (!st) throw new Error("Không tìm thấy sticker ID " + cmd.sticker_id);
          r = await api.sendSticker({ id: Number(st.id || sid), cateId: Number(st.cateId || 0), type: Number(st.type || 1) }, cmd.thread_id, cmd.type);
        } else {
          r = await api.sendMessage({ sticker: cmd.sticker_id }, cmd.thread_id, cmd.type);
        }
        emitResponse(id, true, r);
      } catch (e) {
        log("send_sticker ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
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
      try {
        const r = await api.getGroupInfo(cmd.group_id);
        emitResponse(id, true, r);
      } catch (e) {
        log("get_group_info ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
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

        const wrapper = Buffer.from("aW1wb3J0IHN5cywganNvbiwgaW8sIGJ1aWx0aW5zLCBhc3QNCg0KTUVNX0xJTUlUID0gMTAyNCAqIDEwMjQgKiAxMDI0DQp0cnk6DQogICAgaW1wb3J0IHJlc291cmNlDQogICAgcmVzb3VyY2Uuc2V0cmxpbWl0KHJlc291cmNlLlJMSU1JVF9BUywgKE1FTV9MSU1JVCwgTUVNX0xJTUlUKSkNCmV4Y2VwdCBJbXBvcnRFcnJvcjoNCiAgICBwYXNzDQoNCmRlZiBfaXNfc2FmZV9pZGVudGlmaWVyKHMpOg0KICAgIGlmIG5vdCBzOg0KICAgICAgICByZXR1cm4gRmFsc2UNCiAgICBjMCA9IHNbMF0NCiAgICBpZiBub3QgKGMwLmlzYXNjaWkoKSBhbmQgKGMwLmlzYWxwaGEoKSBvciBjMCA9PSBjaHIoOTUpKSk6DQogICAgICAgIHJldHVybiBGYWxzZQ0KICAgIGZvciBjIGluIHNbMTpdOg0KICAgICAgICBpZiBub3QgKGMuaXNhc2NpaSgpIGFuZCAoYy5pc2FsbnVtKCkgb3IgYyA9PSBjaHIoOTUpKSk6DQogICAgICAgICAgICByZXR1cm4gRmFsc2UNCiAgICByZXR1cm4gVHJ1ZQ0KDQpwYXlsb2FkID0ganNvbi5sb2FkcyhzeXMuc3RkaW4uYnVmZmVyLnJlYWQoKS5kZWNvZGUoInV0Zi04LXNpZyIpKQ0KYXJncyA9IHBheWxvYWQuZ2V0KCJhcmdzIiwgW10pDQpjb2RlID0gcGF5bG9hZC5nZXQoImNvZGUiLCAiIikNCmlzX2FkbWluID0gYm9vbChwYXlsb2FkLmdldCgiYWRtaW4iLCBGYWxzZSkpDQpzdXNwaWNpb24gPSBpbnQocGF5bG9hZC5nZXQoInN1c3BpY2lvbiIsIDApKQ0KYXJnX2lkeCA9IDANCg0KaWYgY29kZS5zdHJpcCgpID09ICJfX3B5X2luZm9fbWFnaWNfN2YzYTliX18iOg0KICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0Ijogc3lzLnZlcnNpb24uc3BsaXQoKVswXSwgImVyciI6ICIifSkuZW5jb2RlKCJ1dGYtOCIpICsgYiJcbiIpDQogICAgc3lzLl9fc3Rkb3V0X18uYnVmZmVyLmZsdXNoKCkNCiAgICBzeXMuZXhpdCgwKQ0KDQppZiBsZW4oY29kZSkgPiAyMDAwIG9yIChub3QgaXNfYWRtaW4gYW5kIHN1c3BpY2lvbiA+PSA3MCk6DQogICAgc3lzLl9fc3Rkb3V0X18uYnVmZmVyLndyaXRlKGpzb24uZHVtcHMoeyJvdXQiOiAiIiwgImVyciI6ICJDb2RlIGJpIGNoYW4ifSkuZW5jb2RlKCJ1dGYtOCIpICsgYiJcbiIpDQogICAgc3lzLl9fc3Rkb3V0X18uYnVmZmVyLmZsdXNoKCkNCiAgICBzeXMuZXhpdCgwKQ0KDQpBTExPV0VEX01PRFVMRVMgPSB7Im1hdGgiLCAicmFuZG9tIiwgInN0cmluZyIsICJkYXRldGltZSIsICJqc29uIiwgInJlIiwgIml0ZXJ0b29scyIsICJjb2xsZWN0aW9ucyIsICJ0aW1lIn0NCkZPUkJJRERFTl9OQU1FUyA9IHsiX19idWlsdGluc19fIiwgIl9faW1wb3J0X18iLCAiZXZhbCIsICJleGVjIiwgImNvbXBpbGUiLCAib3BlbiIsICJnbG9iYWxzIiwgImxvY2FscyIsICJ2YXJzIiwgImRpciIsICJnZXRhdHRyIiwgInNldGF0dHIiLCAiZGVsYXR0ciIsICJicmVha3BvaW50IiwgImV4aXQiLCAicXVpdCIsICJoZWxwIiwgIm1lbW9yeXZpZXciLCAiY2xhc3NtZXRob2QiLCAic3RhdGljbWV0aG9kIiwgInByb3BlcnR5IiwgInN1cGVyIiwgIm9iamVjdCIsICJ0eXBlIiwgIl9fbG9hZGVyX18iLCAiX19zcGVjX18iLCAiX19wYWNrYWdlX18iLCAiX19uYW1lX18iLCAiX19kb2NfXyIsICJfX2ZpbGVfXyIsICJfX2NhY2hlZF9fIiwgIl9fcGF0aF9fIiwgIl9fY2xhc3NfXyIsICJfX2RpY3RfXyIsICJfX2dsb2JhbHNfXyIsICJmX2dsb2JhbHMiLCAiZl9sb2NhbHMiLCAiZl9idWlsdGlucyIsICJnaV9mcmFtZSIsICJjcl9mcmFtZSIsICJhZ19mcmFtZSJ9DQpEQU5HRVJPVVNfQVRUUlMgPSB7ImZvcm1hdCIsICJmb3JtYXRfbWFwIiwgIm1ybyIsICJzdWJjbGFzc2VzIiwgImdpX2ZyYW1lIiwgImNyX2ZyYW1lIiwgImFnX2ZyYW1lIiwgImdpX3J1bm5pbmciLCAiY3JfYXdhaXQiLCAiYWdfYXdhaXQiLCAic2VuZCIsICJ0aHJvdyIsICJjbG9zZSIsICJ0Yl9mcmFtZSIsICJ0Yl9uZXh0IiwgInRiX2xpbmVubyIsICJmX2JhY2siLCAiZl9jb2RlIiwgImZfZ2xvYmFscyIsICJmX2xvY2FscyIsICJmX2J1aWx0aW5zIiwgImZfdHJhY2UiLCAiY29fY29uc3RzIiwgImNvX25hbWVzIiwgImNvX3Zhcm5hbWVzIiwgImNvX2NvZGUiLCAiY29fZnJlZXZhcnMiLCAiY29fY2VsbHZhcnMiLCAiX19yZWR1Y2VfXyIsICJfX3JlZHVjZV9leF9fIiwgIl9fZ2V0YXR0cmlidXRlX18iLCAiX19zZXRhdHRyX18iLCAiX19kZWxhdHRyX18iLCAiX19nZXRzdGF0ZV9fIiwgIl9fc2V0c3RhdGVfXyIsICJfX25ld19fIiwgIl9faW5pdF9fIiwgIl9faW5pdF9zdWJjbGFzc19fIn0NCg0KZGVmIF92YWxpZGF0ZV9hc3Qoc291cmNlKToNCiAgICB0cnk6DQogICAgICAgIHRyZWUgPSBhc3QucGFyc2Uoc291cmNlKQ0KICAgIGV4Y2VwdCBTeW50YXhFcnJvciBhcyBlOg0KICAgICAgICByZXR1cm4gc3RyKGUpDQogICAgZm9yIG5vZGUgaW4gYXN0LndhbGsodHJlZSk6DQogICAgICAgIGlmIGlzaW5zdGFuY2Uobm9kZSwgYXN0LkF0dHJpYnV0ZSk6DQogICAgICAgICAgICBpZiBub3QgX2lzX3NhZmVfaWRlbnRpZmllcihub2RlLmF0dHIpOg0KICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBkdW5nIHRodW9jIHRpbmgga2hvbmcgcGhhaSBBU0NJSSAoVW5pY29kZSBieXBhc3MpIg0KICAgICAgICAgICAgaWYgbm9kZS5hdHRyLnN0YXJ0c3dpdGgoIl9fIikgb3Igbm9kZS5hdHRyIGluIERBTkdFUk9VU19BVFRSUzoNCiAgICAgICAgICAgICAgICByZXR1cm4gIktob25nIGR1b2MgdHJ1eSBjYXAgdGh1b2MgdGluaDogIiArIG5vZGUuYXR0cg0KICAgICAgICBpZiBpc2luc3RhbmNlKG5vZGUsIGFzdC5OYW1lKToNCiAgICAgICAgICAgIGlmIG5vdCBfaXNfc2FmZV9pZGVudGlmaWVyKG5vZGUuaWQpOg0KICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBkdW5nIGJpZW4ga2hvbmcgcGhhaSBBU0NJSSAoVW5pY29kZSBieXBhc3MpIg0KICAgICAgICAgICAgaWYgbm9kZS5pZCBpbiBGT1JCSURERU5fTkFNRVM6DQogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIGR1bmc6ICIgKyBub2RlLmlkDQogICAgICAgIGlmIGlzaW5zdGFuY2Uobm9kZSwgKGFzdC5GdW5jdGlvbkRlZiwgYXN0LkFzeW5jRnVuY3Rpb25EZWYsIGFzdC5DbGFzc0RlZikpOg0KICAgICAgICAgICAgaWYgbm90IF9pc19zYWZlX2lkZW50aWZpZXIobm9kZS5uYW1lKToNCiAgICAgICAgICAgICAgICByZXR1cm4gIktob25nIGR1b2MgZGF0IHRlbiBoYW0vY2xhc3MgYmFuZyBVbmljb2RlIg0KICAgICAgICAgICAgaWYgbm9kZS5uYW1lLnN0YXJ0c3dpdGgoIl9fIik6DQogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIGRpbmggbmdoaWEgaGFtL2NsYXNzIGNvIHRlbiBfXyINCiAgICAgICAgaWYgaXNpbnN0YW5jZShub2RlLCBhc3QuYXJnKToNCiAgICAgICAgICAgIGlmIG5vdCBfaXNfc2FmZV9pZGVudGlmaWVyKG5vZGUuYXJnKToNCiAgICAgICAgICAgICAgICByZXR1cm4gIktob25nIGR1b2MgZGF0IHRlbiB0aGFtIHNvIGJhbmcgVW5pY29kZSINCiAgICAgICAgaWYgaXNpbnN0YW5jZShub2RlLCBhc3QuU3Vic2NyaXB0KToNCiAgICAgICAgICAgIHNsID0gbm9kZS5zbGljZQ0KICAgICAgICAgICAgaWYgaGFzYXR0cihhc3QsICJJbmRleCIpIGFuZCBpc2luc3RhbmNlKHNsLCBhc3QuSW5kZXgpOg0KICAgICAgICAgICAgICAgIHNsID0gc2wudmFsdWUNCiAgICAgICAgICAgIGlmIGlzaW5zdGFuY2Uoc2wsIGFzdC5Db25zdGFudCkgYW5kIGlzaW5zdGFuY2Uoc2wudmFsdWUsIHN0cik6DQogICAgICAgICAgICAgICAgaWYgc2wudmFsdWUuc3RhcnRzd2l0aCgiX18iKSBvciBzbC52YWx1ZSBpbiBEQU5HRVJPVVNfQVRUUlM6DQogICAgICAgICAgICAgICAgICAgIHJldHVybiAiS2hvbmcgZHVvYyBzdWJzY3JpcHQga2V5IGNodW9pIG5ndXkgaGllbSINCiAgICAgICAgaWYgaXNpbnN0YW5jZShub2RlLCBhc3QuSW1wb3J0KToNCiAgICAgICAgICAgIGZvciBhIGluIG5vZGUubmFtZXM6DQogICAgICAgICAgICAgICAgaWYgYS5uYW1lLnNwbGl0KCIuIilbMF0gbm90IGluIEFMTE9XRURfTU9EVUxFUzoNCiAgICAgICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIGltcG9ydDogIiArIGEubmFtZQ0KICAgICAgICBpZiBpc2luc3RhbmNlKG5vZGUsIGFzdC5JbXBvcnRGcm9tKToNCiAgICAgICAgICAgIGlmIG5vZGUubW9kdWxlIGFuZCBub2RlLm1vZHVsZS5zcGxpdCgiLiIpWzBdIG5vdCBpbiBBTExPV0VEX01PRFVMRVM6DQogICAgICAgICAgICAgICAgcmV0dXJuICJLaG9uZyBkdW9jIGltcG9ydDogIiArIG5vZGUubW9kdWxlDQogICAgcmV0dXJuIE5vbmUNCg0KZXJyID0gX3ZhbGlkYXRlX2FzdChjb2RlKQ0KaWYgZXJyOg0KICAgIHN5cy5fX3N0ZG91dF9fLmJ1ZmZlci53cml0ZShqc29uLmR1bXBzKHsib3V0IjogIiIsICJlcnIiOiBlcnJ9KS5lbmNvZGUoInV0Zi04IikgKyBiIlxuIikNCiAgICBzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKQ0KICAgIHN5cy5leGl0KDApDQoNCmNvZGVfb2JqID0gY29tcGlsZShjb2RlLCAiPHB5PiIsICJleGVjIikNCmltcG9ydCBtYXRoLCByYW5kb20sIHN0cmluZywgZGF0ZXRpbWUsIGpzb24gYXMgX3B5anNvbiwgcmUsIGl0ZXJ0b29scywgY29sbGVjdGlvbnMsIHRpbWUNCg0KY2xhc3MgU2VjdXJpdHlFcnJvcihFeGNlcHRpb24pOg0KICAgIHBhc3MNCg0KZGVmIF9mYWtlX2lucHV0KHByb21wdD0iIik6DQogICAgZ2xvYmFsIGFyZ19pZHgNCiAgICBpZiBhcmdfaWR4IDwgbGVuKGFyZ3MpOg0KICAgICAgICB2ID0gYXJnc1thcmdfaWR4XQ0KICAgICAgICBhcmdfaWR4ICs9IDENCiAgICAgICAgcmV0dXJuIHYNCiAgICByYWlzZSBFT0ZFcnJvcigiSGV0IGR1IGxpZXUiKQ0KDQpkZWYgX29yaWdfaW1wb3J0KG5hbWUsICphLCAqKmt3KToNCiAgICBpZiBuYW1lLnNwbGl0KCIuIilbMF0gbm90IGluIEFMTE9XRURfTU9EVUxFUzoNCiAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogS2hvbmcgZHVvYyBpbXBvcnQiKQ0KICAgIHJldHVybiBidWlsdGlucy5fX2ltcG9ydF9fKG5hbWUsICphLCAqKmt3KQ0KDQpkZWYgX3NlY19vcGVuKCphLCAqKmt3KToNCiAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBLaG9uZyBkdW9jIG1vIGZpbGUiKQ0KDQpkZWYgX3NlY19mb3JtYXQoKmEsICoqa3cpOg0KICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IEtob25nIGR1b2MgLmZvcm1hdCgpIikNCg0KX1JFTU9WRSA9ICgib3BlbiIsICJleGVjIiwgImV2YWwiLCAiY29tcGlsZSIsICJfX2ltcG9ydF9fIiwgImdldGF0dHIiLCAic2V0YXR0ciIsICJkZWxhdHRyIiwgInZhcnMiLCAiZ2xvYmFscyIsICJsb2NhbHMiLCAiZGlyIiwgImJyZWFrcG9pbnQiLCAibWVtb3J5dmlldyIsICJvYmplY3QiLCAidHlwZSIpDQpzYWZlX2J1aWx0aW5zID0ge2s6IHYgZm9yIGssIHYgaW4gYnVpbHRpbnMuX19kaWN0X18uaXRlbXMoKSBpZiBrIG5vdCBpbiBfUkVNT1ZFfQ0Kc2FmZV9idWlsdGlucy51cGRhdGUoew0KICAgICJwcmludCI6IHByaW50LCAiaW5wdXQiOiBfZmFrZV9pbnB1dCwgIl9faW1wb3J0X18iOiBfb3JpZ19pbXBvcnQsDQogICAgIm1hdGgiOiBtYXRoLCAicmFuZG9tIjogcmFuZG9tLCAic3RyaW5nIjogc3RyaW5nLCAiZGF0ZXRpbWUiOiBkYXRldGltZSwNCiAgICAianNvbiI6IF9weWpzb24sICJyZSI6IHJlLCAiaXRlcnRvb2xzIjogaXRlcnRvb2xzLA0KICAgICJjb2xsZWN0aW9ucyI6IGNvbGxlY3Rpb25zLCAidGltZSI6IHRpbWUsDQogICAgIm9wZW4iOiBfc2VjX29wZW4sICJmb3JtYXQiOiBfc2VjX2Zvcm1hdCwNCiAgICAiVHJ1ZSI6IFRydWUsICJGYWxzZSI6IEZhbHNlLCAiTm9uZSI6IE5vbmUsDQogICAgImludCI6IGludCwgImZsb2F0IjogZmxvYXQsICJzdHIiOiBzdHIsICJib29sIjogYm9vbCwNCiAgICAibGlzdCI6IGxpc3QsICJkaWN0IjogZGljdCwgInR1cGxlIjogdHVwbGUsICJzZXQiOiBzZXQsICJmcm96ZW5zZXQiOiBmcm96ZW5zZXQsDQogICAgImJ5dGVzIjogYnl0ZXMsICJieXRlYXJyYXkiOiBieXRlYXJyYXksDQogICAgInJhbmdlIjogcmFuZ2UsICJsZW4iOiBsZW4sICJtaW4iOiBtaW4sICJtYXgiOiBtYXgsICJzdW0iOiBzdW0sICJhYnMiOiBhYnMsDQogICAgInNvcnRlZCI6IHNvcnRlZCwgInJldmVyc2VkIjogcmV2ZXJzZWQsICJlbnVtZXJhdGUiOiBlbnVtZXJhdGUsICJ6aXAiOiB6aXAsDQogICAgIm1hcCI6IG1hcCwgImZpbHRlciI6IGZpbHRlciwgImFsbCI6IGFsbCwgImFueSI6IGFueSwNCiAgICAiaXNpbnN0YW5jZSI6IGlzaW5zdGFuY2UsICJpc3N1YmNsYXNzIjogaXNzdWJjbGFzcywgImNhbGxhYmxlIjogY2FsbGFibGUsDQogICAgIml0ZXIiOiBpdGVyLCAibmV4dCI6IG5leHQsICJyZXByIjogcmVwciwgImhhc2giOiBoYXNoLCAiaWQiOiBpZCwNCiAgICAiY2hyIjogY2hyLCAib3JkIjogb3JkLCAiaGV4IjogaGV4LCAiYmluIjogYmluLCAib2N0Ijogb2N0LA0KICAgICJkaXZtb2QiOiBkaXZtb2QsICJwb3ciOiBwb3csICJyb3VuZCI6IHJvdW5kLA0KICAgICJFeGNlcHRpb24iOiBFeGNlcHRpb24sICJWYWx1ZUVycm9yIjogVmFsdWVFcnJvciwgIlR5cGVFcnJvciI6IFR5cGVFcnJvciwNCiAgICAiS2V5RXJyb3IiOiBLZXlFcnJvciwgIkluZGV4RXJyb3IiOiBJbmRleEVycm9yLCAiUnVudGltZUVycm9yIjogUnVudGltZUVycm9yLA0KICAgICJaZXJvRGl2aXNpb25FcnJvciI6IFplcm9EaXZpc2lvbkVycm9yLCAiQXR0cmlidXRlRXJyb3IiOiBBdHRyaWJ1dGVFcnJvciwNCiAgICAiTmFtZUVycm9yIjogTmFtZUVycm9yLCAiSW1wb3J0RXJyb3IiOiBJbXBvcnRFcnJvciwgIlN0b3BJdGVyYXRpb24iOiBTdG9wSXRlcmF0aW9uLA0KICAgICJBcml0aG1ldGljRXJyb3IiOiBBcml0aG1ldGljRXJyb3IsICJMb29rdXBFcnJvciI6IExvb2t1cEVycm9yLA0KICAgICJOb3RJbXBsZW1lbnRlZEVycm9yIjogTm90SW1wbGVtZW50ZWRFcnJvciwgIlBlcm1pc3Npb25FcnJvciI6IFBlcm1pc3Npb25FcnJvciwNCiAgICAiRU9GRXJyb3IiOiBFT0ZFcnJvciwgIkZpbGVOb3RGb3VuZEVycm9yIjogRmlsZU5vdEZvdW5kRXJyb3IsDQogICAgIk9TRXJyb3IiOiBPU0Vycm9yLCAiSU9FcnJvciI6IElPRXJyb3IsDQp9KQ0KDQpfaG9va19lbmFibGVkID0gW0ZhbHNlXQ0KX2FsbG93X2V4ZWMgPSBbRmFsc2VdDQpCTE9DS0VEX1BSRUZJWEVTID0gKA0KICAgICJvcy4iLCAic3VicHJvY2Vzcy4iLCAic29ja2V0LiIsICJjdHlwZXMuIiwgInNodXRpbC4iLCAidGVtcGZpbGUuIiwNCiAgICAid2lucmVnLiIsICJfd2luYXBpLiIsICJtbWFwLiIsICJzcWxpdGUzLiIsICJ1cmxsaWIuIiwgImh0dHAuIiwNCiAgICAiZnRwbGliLiIsICJzbXRwbGliLiIsICJ0ZWxuZXRsaWIuIiwgImltYXBsaWIuIiwgInBvcGxpYi4iLCAibm50cGxpYi4iLA0KICAgICJwaWNrbGUuIiwgIm1hcnNoYWwuIiwgInNoZWx2ZS4iLCAiZGJtLiIsICJ6aXBmaWxlLiIsICJ0YXJmaWxlLiIsDQogICAgImd6aXAuIiwgImJ6Mi4iLCAibHptYS4iLCAiY3B5dGhvbi4iLA0KKQ0KQkxPQ0tFRF9FVkVOVFMgPSB7DQogICAgImV4ZWMiLCAiY29tcGlsZSIsICJvcGVuIiwgIm9iamVjdC5fX3NldGF0dHJfXyIsICJvYmplY3QuX19kZWxhdHRyX18iLA0KICAgICJzeXMuYWRkYXVkaXRob29rIiwgInN5cy5zZXR0cmFjZSIsICJzeXMuc2V0cHJvZmlsZSIsDQogICAgInN5cy5zZXRfYXN5bmNnZW5faG9va3MiLCAic3lzLnVucmFpc2FibGVob29rIiwNCn0NCg0KZGVmIF9hdWRpdF9ob29rKGV2ZW50LCBldl9hcmdzKToNCiAgICBpZiBub3QgX2hvb2tfZW5hYmxlZFswXToNCiAgICAgICAgcmV0dXJuDQogICAgaWYgZXZlbnQgPT0gImV4ZWMiIGFuZCBfYWxsb3dfZXhlY1swXToNCiAgICAgICAgX2FsbG93X2V4ZWNbMF0gPSBGYWxzZQ0KICAgICAgICByZXR1cm4NCiAgICBpZiBldmVudCBpbiBCTE9DS0VEX0VWRU5UUzoNCiAgICAgICAgcmFpc2UgU2VjdXJpdHlFcnJvcigiQmFvIG1hdDogQmxvY2tlZCBldmVudCAiICsgZXZlbnQpDQogICAgaWYgZXZlbnQgPT0gImltcG9ydCI6DQogICAgICAgIG1vZCA9IChldl9hcmdzWzBdIG9yICIiKS5zcGxpdCgiLiIpWzBdIGlmIGV2X2FyZ3MgZWxzZSAiIg0KICAgICAgICBpZiBtb2Qgbm90IGluIEFMTE9XRURfTU9EVUxFUzoNCiAgICAgICAgICAgIHJhaXNlIFNlY3VyaXR5RXJyb3IoIkJhbyBtYXQ6IGltcG9ydCBibG9ja2VkIikNCiAgICBpZiBldmVudC5zdGFydHN3aXRoKEJMT0NLRURfUFJFRklYRVMpOg0KICAgICAgICByYWlzZSBTZWN1cml0eUVycm9yKCJCYW8gbWF0OiBCbG9ja2VkIHByZWZpeCAiICsgZXZlbnQpDQoNCnN5cy5hZGRhdWRpdGhvb2soX2F1ZGl0X2hvb2spDQpzdGRvdXRfY2FwdHVyZSA9IGlvLlN0cmluZ0lPKCkNCnN0ZGVycl9jYXB0dXJlID0gaW8uU3RyaW5nSU8oKQ0Kc3lzLnN0ZG91dCA9IHN0ZG91dF9jYXB0dXJlDQpzeXMuc3RkZXJyID0gc3RkZXJyX2NhcHR1cmUNCg0KX2hvb2tfZW5hYmxlZFswXSA9IFRydWUNCl9hbGxvd19leGVjWzBdID0gVHJ1ZQ0KdHJ5Og0KICAgIF9nID0geyJfX2J1aWx0aW5zX18iOiBzYWZlX2J1aWx0aW5zLCAiX19uYW1lX18iOiAiX19tYWluX18ifQ0KICAgIHRyeToNCiAgICAgICAgZXhlYyhjb2RlX29iaiwgX2cpDQogICAgZmluYWxseToNCiAgICAgICAgX2FsbG93X2V4ZWNbMF0gPSBGYWxzZQ0KZXhjZXB0IFNlY3VyaXR5RXJyb3IgYXMgZToNCiAgICBzeXMuc3RkZXJyLndyaXRlKHN0cihlKSkNCmV4Y2VwdCBNZW1vcnlFcnJvcjoNCiAgICBzeXMuc3RkZXJyLndyaXRlKCJWdW90IHF1YSBibyBuaG8gMUdCIikNCmV4Y2VwdCBCYXNlRXhjZXB0aW9uIGFzIGU6DQogICAgX2hvb2tfZW5hYmxlZFswXSA9IEZhbHNlDQogICAgX2FsbG93X2V4ZWNbMF0gPSBGYWxzZQ0KICAgIHRyeToNCiAgICAgICAgc3lzLnN0ZGVyci53cml0ZSh0eXBlKGUpLl9fbmFtZV9fICsgIjogIiArIChzdHIoZSkgaWYgc3RyKGUpIGVsc2UgIiIpKQ0KICAgIGV4Y2VwdDoNCiAgICAgICAgc3lzLnN0ZGVyci53cml0ZSh0eXBlKGUpLl9fbmFtZV9fKQ0KZmluYWxseToNCiAgICBfaG9va19lbmFibGVkWzBdID0gRmFsc2UNCg0KX291dCA9IHN0ZG91dF9jYXB0dXJlLmdldHZhbHVlKCkNCl9lcnIgPSBzdGRlcnJfY2FwdHVyZS5nZXR2YWx1ZSgpDQppZiBsZW4oX291dCkgPiA0MDAwOg0KICAgIF9vdXQgPSBfb3V0Wzo0MDAwXSArIGNocigxMCkgKyAiLi4uKHF1YSBkYWkpIg0KDQpzeXMuX19zdGRvdXRfXy5idWZmZXIud3JpdGUoanNvbi5kdW1wcyh7Im91dCI6IF9vdXQsICJlcnIiOiBfZXJyfSkuZW5jb2RlKCJ1dGYtOCIpICsgYiJcbiIpDQpzeXMuX19zdGRvdXRfXy5idWZmZXIuZmx1c2goKQ==", "base64").toString("utf8");

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
            emitResponse(id, false, "Lỗi: Không tìm thấy Python trong PATH.");
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

            else if (cmd.action === "translate_text") {
      try {
        const text = String(cmd.text || "").trim();
        const to = String(cmd.to || "vi").trim() || "vi";
        if (!text) return emitResponse(id, false, "Van ban rong");
        const url = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl="
                  + encodeURIComponent(to) + "&dt=t&q=" + encodeURIComponent(text);
        const res = await fetch(url, { headers: { "User-Agent": "Mozilla/5.0" } });
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();
        const translated = Array.isArray(data?.[0]) ? data[0].map(x => x?.[0] || "").join("") : "";
        const detected = String(data?.[2] || "auto");
        emitResponse(id, true, { ok: true, text: translated, from: detected, to });
      } catch (e) {
        log("translate_text ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
      }
    }
    else if (cmd.action === "get_weather") {
      try {
        const city = String(cmd.city || "Hanoi").trim();
        const geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name="
                     + encodeURIComponent(city) + "&count=1&language=vi&format=json";
        const gRes = await fetch(geoUrl);
        if (!gRes.ok) throw new Error("Geo HTTP " + gRes.status);
        const gData = await gRes.json();
        const loc = gData?.results?.[0];
        if (!loc) return emitResponse(id, false, "Khong tim thay dia diem: " + city);
        const wUrl = `https://api.open-meteo.com/v1/forecast?latitude=${loc.latitude}&longitude=${loc.longitude}`
                   + `&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m`
                   + `&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1`;
        const wRes = await fetch(wUrl);
        if (!wRes.ok) throw new Error("Weather HTTP " + wRes.status);
        const wData = await wRes.json();
        emitResponse(id, true, {
          ok: true,
          name: loc.name || city,
          country: loc.country || "",
          current: wData.current || {},
          daily: wData.daily || {}
        });
      } catch (e) {
        log("get_weather ERROR: " + (e?.message || String(e)));
        emitResponse(id, false, e?.message || String(e));
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

