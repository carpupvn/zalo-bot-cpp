$ErrorActionPreference = 'SilentlyContinue'
$logPath = "$PSScriptRoot\..\data\py_audit.log"
while ($true) {
    $pyProcs = Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" -ErrorAction SilentlyContinue
    foreach ($p in $pyProcs) {
        if ($p.CommandLine -match "import os|subprocess|socket|open\(") {
            Stop-Process -Id $p.ProcessId -Force
        }
        $conns = Get-NetTCPConnection -OwningProcess $p.ProcessId -ErrorAction SilentlyContinue
        foreach ($c in $conns) {
            if ($c.RemoteAddress -ne "127.0.0.1" -and $c.RemoteAddress -ne "::1") {
                Stop-Process -Id $p.ProcessId -Force
            }
        }
    }
    Start-Sleep -Milliseconds 500
}