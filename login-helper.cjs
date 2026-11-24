const fs = require('fs');
const path = require('path');
const os = require('os');

// ============================================================
// FAKE HOME & PERSISTENT IMEI
// ============================================================
const rawCookie = process.argv[2] || '';
const rawQr = process.argv[3] || '';
if (!rawCookie || !rawQr) { console.error('Missing args'); process.exit(1); }

const cookieOut = path.resolve(rawCookie);
const qrPath = path.resolve(rawQr);
const logPath = cookieOut.replace(/\.json$/, '.log');
const deviceFile = cookieOut.replace(/\.json$/, '.device.json');

function log(msg) {
    const line = '[' + new Date().toISOString() + '] ' + msg;
    console.log(line);
    try { fs.appendFileSync(logPath, line + '\n'); } catch (e) {}
}

function randomHex(len) {
    let s = '';
    const chars = '0123456789abcdef';
    for (let i = 0; i < len; i++) s += chars[Math.floor(Math.random() * 16)];
    return s;
}

function randomImei() {
    let imei = '3' + Math.floor(Math.random() * 9 + 1);
    for (let i = 0; i < 12; i++) imei += Math.floor(Math.random() * 10);
    return imei;
}

// Đọc hoặc tạo mới IMEI CỐ ĐỊNH cho profile này
let deviceData;
if (fs.existsSync(deviceFile)) {
    try {
        deviceData = JSON.parse(fs.readFileSync(deviceFile, 'utf8'));
        log('Loaded EXISTING device ID: ' + deviceData.imei);
    } catch (e) { deviceData = null; }
}

if (!deviceData) {
    deviceData = {
        imei: randomImei(),
        deviceId: randomHex(32),
        androidId: randomHex(16),
        mac: Array.from({length: 6}, () => randomHex(2).toUpperCase()).join(':')
    };
    try { fs.mkdirSync(path.dirname(deviceFile), { recursive: true }); } catch (e) {}
    fs.writeFileSync(deviceFile, JSON.stringify(deviceData, null, 2));
    log('Generated NEW device ID: ' + deviceData.imei + ' and saved.');
}

// Fake Home để tránh dính cache session cũ
const fakeHome = path.join(os.tmpdir(), 'zalo-fake-home-' + Date.now());
fs.mkdirSync(fakeHome, { recursive: true });
const origHomedir = os.homedir;
os.homedir = () => fakeHome;
process.env.HOME = fakeHome;
process.env.USERPROFILE = fakeHome;
process.env.XDG_CONFIG_HOME = fakeHome;
process.env.XDG_CACHE_HOME = fakeHome;
process.env.XDG_DATA_HOME = fakeHome;
process.env.APPDATA = fakeHome;
process.env.LOCALAPPDATA = fakeHome;

try { fs.mkdirSync(path.dirname(cookieOut), { recursive: true }); } catch (e) {}
try { fs.mkdirSync(path.dirname(qrPath), { recursive: true }); } catch (e) {}
try { fs.unlinkSync(qrPath); } catch (e) {}
try { process.chdir(fakeHome); } catch (e) {}

// Override User-Agent
const FAKE_UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36';
const https = require('https');
const http = require('http');
function patchModule(mod) {
    const origRequest = mod.request;
    mod.request = function(options, callback) {
        if (typeof options === 'string' || options instanceof URL) {
            options = typeof options === 'string' ? new URL(options) : options;
            return origRequest.call(this, { hostname: options.hostname, port: options.port, path: options.pathname + options.search, method: 'GET', headers: { 'User-Agent': FAKE_UA } }, callback);
        }
        options.headers = options.headers || {};
        options.headers['User-Agent'] = FAKE_UA;
        return origRequest.call(this, options, callback);
    };
}
patchModule(https); patchModule(http);

const timeoutTimer = setTimeout(() => { log('TIMEOUT 180s'); process.exit(2); }, 180000);

(async () => {
    try {
        const zca = require('zca-js');
        const ZaloClass = zca.Zalo || (zca.default && zca.default.Zalo);
        
        // Truyền IMEI cố định vào constructor
        const zalo = new ZaloClass({
            logging: true,
            selfListen: true,
            checkUpdate: false,
            imei: deviceData.imei,
            userAgent: FAKE_UA,
        });
        
        try {
            if (zalo.imei !== undefined) zalo.imei = deviceData.imei;
            if (zalo.userAgent !== undefined) zalo.userAgent = FAKE_UA;
            if (zalo.context && zalo.context.imei !== undefined) zalo.context.imei = deviceData.imei;
        } catch (e) {}

        log('Calling loginQR with persistent IMEI...');
        let api = null;
        try { api = await zalo.loginQR({ qrPath: qrPath }); } 
        catch (e1) { api = await zalo.loginQR(qrPath); }
        
        clearTimeout(timeoutTimer);
        
        const cookie = (api.getCookie && api.getCookie()) || api.cookie;
        const ctx = (api.getContext && api.getContext()) || {};
        const imei = ctx.imei || deviceData.imei;
        const uid = String((api.getOwnId && api.getOwnId()) || ctx.uid || '');
        
        log('Login result: uid=' + uid + ' imei=' + imei);
        
        let cookieStr = '';
        if (cookie && typeof cookie.serializeSync === 'function') cookieStr = cookie.serializeSync();
        else if (typeof cookie === 'string') cookieStr = cookie;
        else cookieStr = JSON.stringify(cookie);
        
        let finalCookie;
        try {
            // Luôn dùng array_object format (tương thích bridge.js)
            finalCookie = cookieStr.split(/;\s*/).filter(s => s.includes('=')).map(p => {
                const eq = p.indexOf('=');
                return {
                    name: p.substring(0, eq).trim(),
                    value: p.substring(eq + 1),
                    domain: '.zalo.me',
                    path: '/'
                };
            });
        } catch (e) { finalCookie = cookieStr; }
        
        // Kiem tra xem co quet nham acc chinh khong (UID cua profile 1)
        let isMainAccount = false;
        try {
            const p1Meta = path.join(path.dirname(cookieOut), '1.meta.json');
            if (fs.existsSync(p1Meta)) {
                const mainMeta = JSON.parse(fs.readFileSync(p1Meta, 'utf8'));
                if (mainMeta.uid === uid) isMainAccount = true;
            }
        } catch(e) {}
        
        if (isMainAccount) {
            log('!!! CANH BAO: Ban vua quet QR bang ACC CHINH (Profile 1).');
            log('!!! File cookie se KHONG duoc luu de tranh ghi de.');
            log('!!! Vui long dang xuat acc chinh tren dien thoai, sau do quet lai bang acc khac.');
            process.exit(3); // Ma loi rieng cho truong hop nay
        }

        // Ghi cookie (Atomic Write)
        fs.writeFileSync(cookieOut + '.tmp', JSON.stringify({ cookie: finalCookie, imei }, null, 2));
        fs.renameSync(cookieOut + '.tmp', cookieOut);
        fs.writeFileSync(cookieOut.replace(/\.json$/, '.meta.json'), JSON.stringify({ uid: uid, last_used: Math.floor(Date.now() / 1000), user_agent: FAKE_UA }, null, 2));
        
        log('=== SUCCESS ===');
        process.exit(0);
    } catch (e) {
        log('FAILED: ' + (e && e.stack ? e.stack : String(e)));
        clearTimeout(timeoutTimer);
        process.exit(1);
    }
})();