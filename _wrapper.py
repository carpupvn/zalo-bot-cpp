import sys, json, io, builtins, ast

MEM_LIMIT = 1024 * 1024 * 1024
try:
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (MEM_LIMIT, MEM_LIMIT))
except ImportError:
    pass

def _is_safe_identifier(s):
    if not s:
        return False
    c0 = s[0]
    if not (c0.isascii() and (c0.isalpha() or c0 == chr(95))):
        return False
    for c in s[1:]:
        if not (c.isascii() and (c.isalnum() or c == chr(95))):
            return False
    return True

payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
args = payload.get("args", [])
code = payload.get("code", "")
is_admin = bool(payload.get("admin", False))
suspicion = int(payload.get("suspicion", 0))
arg_idx = 0

if code.strip() == "__py_info_magic_7f3a9b__":
    sys.__stdout__.buffer.write(json.dumps({"out": sys.version.split()[0], "err": ""}).encode("utf-8") + b"\n")
    sys.__stdout__.buffer.flush()
    sys.exit(0)

if len(code) > 2000 or (not is_admin and suspicion >= 70):
    sys.__stdout__.buffer.write(json.dumps({"out": "", "err": "Code bi chan"}).encode("utf-8") + b"\n")
    sys.__stdout__.buffer.flush()
    sys.exit(0)

ALLOWED_MODULES = {"math", "random", "string", "datetime", "json", "re", "itertools", "collections", "time"}
FORBIDDEN_NAMES = {"__builtins__", "__import__", "eval", "exec", "compile", "open", "globals", "locals", "vars", "dir", "getattr", "setattr", "delattr", "breakpoint", "exit", "quit", "help", "memoryview", "classmethod", "staticmethod", "property", "super", "object", "type", "__loader__", "__spec__", "__package__", "__name__", "__doc__", "__file__", "__cached__", "__path__", "__class__", "__dict__", "__globals__", "f_globals", "f_locals", "f_builtins", "gi_frame", "cr_frame", "ag_frame"}
DANGEROUS_ATTRS = {"format", "format_map", "mro", "subclasses", "gi_frame", "cr_frame", "ag_frame", "gi_running", "cr_await", "ag_await", "send", "throw", "close", "tb_frame", "tb_next", "tb_lineno", "f_back", "f_code", "f_globals", "f_locals", "f_builtins", "f_trace", "co_consts", "co_names", "co_varnames", "co_code", "co_freevars", "co_cellvars", "__reduce__", "__reduce_ex__", "__getattribute__", "__setattr__", "__delattr__", "__getstate__", "__setstate__", "__new__", "__init__", "__init_subclass__"}

def _validate_ast(source):
    try:
        tree = ast.parse(source)
    except SyntaxError as e:
        return str(e)
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute):
            if not _is_safe_identifier(node.attr):
                return "Khong duoc dung thuoc tinh khong phai ASCII (Unicode bypass)"
            if node.attr.startswith("__") or node.attr in DANGEROUS_ATTRS:
                return "Khong duoc truy cap thuoc tinh: " + node.attr
        if isinstance(node, ast.Name):
            if not _is_safe_identifier(node.id):
                return "Khong duoc dung bien khong phai ASCII (Unicode bypass)"
            if node.id in FORBIDDEN_NAMES:
                return "Khong duoc dung: " + node.id
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            if not _is_safe_identifier(node.name):
                return "Khong duoc dat ten ham/class bang Unicode"
            if node.name.startswith("__"):
                return "Khong duoc dinh nghia ham/class co ten __"
        if isinstance(node, ast.arg):
            if not _is_safe_identifier(node.arg):
                return "Khong duoc dat ten tham so bang Unicode"
        if isinstance(node, ast.Subscript):
            sl = node.slice
            if hasattr(ast, "Index") and isinstance(sl, ast.Index):
                sl = sl.value
            if isinstance(sl, ast.Constant) and isinstance(sl.value, str):
                if sl.value.startswith("__") or sl.value in DANGEROUS_ATTRS:
                    return "Khong duoc subscript key chuoi nguy hiem"
        if isinstance(node, ast.Import):
            for a in node.names:
                if a.name.split(".")[0] not in ALLOWED_MODULES:
                    return "Khong duoc import: " + a.name
        if isinstance(node, ast.ImportFrom):
            if node.module and node.module.split(".")[0] not in ALLOWED_MODULES:
                return "Khong duoc import: " + node.module
    return None

err = _validate_ast(code)
if err:
    sys.__stdout__.buffer.write(json.dumps({"out": "", "err": err}).encode("utf-8") + b"\n")
    sys.__stdout__.buffer.flush()
    sys.exit(0)

code_obj = compile(code, "<py>", "exec")
import math, random, string, datetime, json as _pyjson, re, itertools, collections, time

class SecurityError(Exception):
    pass

def _fake_input(prompt=""):
    global arg_idx
    if arg_idx < len(args):
        v = args[arg_idx]
        arg_idx += 1
        return v
    raise EOFError("Het du lieu")

def _orig_import(name, *a, **kw):
    if name.split(".")[0] not in ALLOWED_MODULES:
        raise SecurityError("Bao mat: Khong duoc import")
    return builtins.__import__(name, *a, **kw)

def _sec_open(*a, **kw):
    raise SecurityError("Bao mat: Khong duoc mo file")

def _sec_format(*a, **kw):
    raise SecurityError("Bao mat: Khong duoc .format()")

_REMOVE = ("open", "exec", "eval", "compile", "__import__", "getattr", "setattr", "delattr", "vars", "globals", "locals", "dir", "breakpoint", "memoryview", "object", "type")
safe_builtins = {k: v for k, v in builtins.__dict__.items() if k not in _REMOVE}
safe_builtins.update({
    "print": print, "input": _fake_input, "__import__": _orig_import,
    "math": math, "random": random, "string": string, "datetime": datetime,
    "json": _pyjson, "re": re, "itertools": itertools,
    "collections": collections, "time": time,
    "open": _sec_open, "format": _sec_format,
    "True": True, "False": False, "None": None,
    "int": int, "float": float, "str": str, "bool": bool,
    "list": list, "dict": dict, "tuple": tuple, "set": set, "frozenset": frozenset,
    "bytes": bytes, "bytearray": bytearray,
    "range": range, "len": len, "min": min, "max": max, "sum": sum, "abs": abs,
    "sorted": sorted, "reversed": reversed, "enumerate": enumerate, "zip": zip,
    "map": map, "filter": filter, "all": all, "any": any,
    "isinstance": isinstance, "issubclass": issubclass, "callable": callable,
    "iter": iter, "next": next, "repr": repr, "hash": hash, "id": id,
    "chr": chr, "ord": ord, "hex": hex, "bin": bin, "oct": oct,
    "divmod": divmod, "pow": pow, "round": round,
    "Exception": Exception, "ValueError": ValueError, "TypeError": TypeError,
    "KeyError": KeyError, "IndexError": IndexError, "RuntimeError": RuntimeError,
    "ZeroDivisionError": ZeroDivisionError, "AttributeError": AttributeError,
    "NameError": NameError, "ImportError": ImportError, "StopIteration": StopIteration,
    "ArithmeticError": ArithmeticError, "LookupError": LookupError,
    "NotImplementedError": NotImplementedError, "PermissionError": PermissionError,
    "EOFError": EOFError, "FileNotFoundError": FileNotFoundError,
    "OSError": OSError, "IOError": IOError,
})

_hook_enabled = [False]
_allow_exec = [False]
BLOCKED_PREFIXES = (
    "os.", "subprocess.", "socket.", "ctypes.", "shutil.", "tempfile.",
    "winreg.", "_winapi.", "mmap.", "sqlite3.", "urllib.", "http.",
    "ftplib.", "smtplib.", "telnetlib.", "imaplib.", "poplib.", "nntplib.",
    "pickle.", "marshal.", "shelve.", "dbm.", "zipfile.", "tarfile.",
    "gzip.", "bz2.", "lzma.", "cpython.",
)
BLOCKED_EVENTS = {
    "exec", "compile", "open", "object.__setattr__", "object.__delattr__",
    "sys.addaudithook", "sys.settrace", "sys.setprofile",
    "sys.set_asyncgen_hooks", "sys.unraisablehook",
}

def _audit_hook(event, ev_args):
    if not _hook_enabled[0]:
        return
    if event == "exec" and _allow_exec[0]:
        _allow_exec[0] = False
        return
    if event in BLOCKED_EVENTS:
        raise SecurityError("Bao mat: Blocked event " + event)
    if event == "import":
        mod = (ev_args[0] or "").split(".")[0] if ev_args else ""
        if mod not in ALLOWED_MODULES:
            raise SecurityError("Bao mat: import blocked")
    if event.startswith(BLOCKED_PREFIXES):
        raise SecurityError("Bao mat: Blocked prefix " + event)

sys.addaudithook(_audit_hook)
stdout_capture = io.StringIO()
stderr_capture = io.StringIO()
sys.stdout = stdout_capture
sys.stderr = stderr_capture

_hook_enabled[0] = True
_allow_exec[0] = True
try:
    _g = {"__builtins__": safe_builtins, "__name__": "__main__"}
    try:
        exec(code_obj, _g)
    finally:
        _allow_exec[0] = False
except SecurityError as e:
    sys.stderr.write(str(e))
except MemoryError:
    sys.stderr.write("Vuot qua bo nho 1GB")
except BaseException as e:
    _hook_enabled[0] = False
    _allow_exec[0] = False
    try:
        sys.stderr.write(type(e).__name__ + ": " + (str(e) if str(e) else ""))
    except:
        sys.stderr.write(type(e).__name__)
finally:
    _hook_enabled[0] = False

_out = stdout_capture.getvalue()
_err = stderr_capture.getvalue()
if len(_out) > 4000:
    _out = _out[:4000] + chr(10) + "...(qua dai)"

sys.__stdout__.buffer.write(json.dumps({"out": _out, "err": _err}).encode("utf-8") + b"\n")
sys.__stdout__.buffer.flush()