const fs = require('fs');
const path = require('path');
const os = require('os');

const rawCookie = process.argv[2] || '';
const rawQr = process.argv[3] || '';
if (!rawCookie || !rawQr) { console.error('Missing args'); process.exit(1); }

const cookieOut = path.resolve(rawCookie);
const qrPath = path.resolve(rawQr);
const logPath = cookieOut.replace(/\.json$/, '.log');

function log(msg) {
    const line = '[' + new Date().toISOString() + '] ' + msg + '\n';
    try { fs.appendFileSync(logPath, line); } catch (e) {}
    console.log(msg);
}

log('=== HELPER START ===');

// ============ DETECT FORMAT tu config.json goc ============
let refFormat = 'string';
let refArray = null;
try {
    const refPath = path.join(path.dirname(rawCookie), '..', '..', 'config.json');
    log('looking for ref config at: ' + refPath);
    if (fs.existsSync(refPath)) {
        const ref = JSON.parse(fs.readFileSync(refPath, 'utf8'));
        if (Array.isArray(ref.cookie)) {
            if (ref.cookie.length > 0 && typeof ref.cookie[0] === 'string') {
                refFormat = 'array_string';
                // Sample: "zpdid=abc", split at '='
                const firstEq = ref.cookie[0].indexOf('=');
                log('sample elem: ' + ref.cookie[0].substring(0, Math.min(30, firstEq+5)) + '...');
            } else if (ref.cookie.length > 0 && typeof ref.cookie[0] === 'object') {
                refFormat = 'array_object';
                refArray = ref.cookie[0];
                log('sample obj keys: ' + Object.keys(refArray).join(','));
            } else {
                refFormat = 'array_string';
            }
        } else if (typeof ref.cookie === 'string') {
            refFormat = 'string';
        }
        log('DETECTED FORMAT: ' + refFormat);
    } else {
        log('No config.json ref found, default to string');
    }
} catch (e) {
    log('format detect error: ' + e.message);
}

// Tạo thư mục
try { fs.mkdirSync(path.dirname(cookieOut), { recursive: true }); } catch(e) {}
try { fs.mkdirSync(path.dirname(qrPath), { recursive: true }); } catch(e) {}
try { fs.unlinkSync(qrPath); } catch(e) {}

// Chdir vào tmp để zca-js không đọc cookie cũ
const tmpDir = path.join(os.tmpdir(), 'zalo-login-' + Date.now());
try { fs.mkdirSync(tmpDir, { recursive: true }); process.chdir(tmpDir); } catch(e) {}
log('chdir to ' + process.cwd());

const timer = setTimeout(() => { log('TIMEOUT 180s'); process.exit(2); }, 180000);

(async () => {
    try {
        const zca = require('zca-js');
        const ZaloClass = zca.Zalo || (zca.default && zca.default.Zalo);
        if (!ZaloClass) throw new Error('No Zalo class');

        const zalo = new ZaloClass({ logging: true, selfListen: true });
        log('calling loginQR...');
        let api;
        try { api = await zalo.loginQR({ qrPath }); }
        catch(e1) { log('style1 fail: ' + e1.message); api = await zalo.loginQR(qrPath); }
        log('loginQR OK');
        clearTimeout(timer);

        const cookieObj = (api.getCookie && api.getCookie()) || api.cookie;
        const ctx = (api.getContext && api.getContext()) || {};
        const imei = ctx.imei || '';
        const uid = (api.getOwnId && api.getOwnId()) || ctx.uid || '';

        // Serialize cookie thanh string truoc
        let cookieStr = '';
        if (cookieObj && typeof cookieObj.serializeSync === 'function') {
            cookieStr = cookieObj.serializeSync();
        } else if (typeof cookieObj === 'string') {
            cookieStr = cookieObj;
        } else {
            cookieStr = JSON.stringify(cookieObj);
        }
        log('cookieStr length=' + cookieStr.length);
        log('cookieStr sample: ' + cookieStr.substring(0, 100));

        // Convert sang format giong config.json goc
        let finalCookie;
        if (refFormat === 'array_string') {
            // Split "a=b; c=d" thanh ["a=b", "c=d"]
            finalCookie = cookieStr.split(/;\s*/).filter(s => s.includes('=')).map(s => s.trim());
            log('split -> array[' + finalCookie.length + ']');
            if (finalCookie.length > 0) log('first: ' + finalCookie[0].substring(0, 40));
        } else if (refFormat === 'array_object') {
            // Tao mang {name, value, ...} tu string
            const pairs = cookieStr.split(/;\s*/).filter(s => s.includes('='));
            finalCookie = pairs.map(p => {
                const eq = p.indexOf('=');
                const name = p.substring(0, eq).trim();
                const value = p.substring(eq + 1);
                // Copy cau truc tu ref neu co
                if (refArray && refArray.domain) {
                    return { name, value, domain: refArray.domain };
                }
                return { name, value };
            });
            log('split -> array_object[' + finalCookie.length + ']');
        } else {
            finalCookie = cookieStr;
            log('keeping as string');
        }

        const payload = { cookie: finalCookie, imei: imei };
        fs.writeFileSync(cookieOut + '.tmp', JSON.stringify(payload, null, 2));
        fs.renameSync(cookieOut + '.tmp', cookieOut);

        fs.writeFileSync(cookieOut.replace(/\.json$/, '.meta.json'),
            JSON.stringify({ uid: String(uid), last_used: Math.floor(Date.now() / 1000) }));

        log('=== SUCCESS uid=' + uid + ' format=' + refFormat + ' ===');
        process.exit(0);
    } catch(e) {
        log('FAILED: ' + (e && e.stack ? e.stack : String(e)));
        clearTimeout(timer);
        process.exit(1);
    }
})();