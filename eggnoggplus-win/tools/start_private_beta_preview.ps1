[CmdletBinding()]
param([int]$Port = 47884)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$privateRoot = Join-Path $repo 'build\private_beta'
$webRoot = Join-Path $privateRoot 'preview\releases'
if (-not (Test-Path -LiteralPath (Join-Path $webRoot 'beta.json'))) {
    throw 'Private beta preview files have not been prepared.'
}
$python = Get-Command python.exe -ErrorAction Stop
$url = "http://127.0.0.1:$Port/latest.json"
try {
    $existing = Invoke-WebRequest -UseBasicParsing -Uri $url -TimeoutSec 2
    $manifest = $existing.Content | ConvertFrom-Json
    if ($manifest.version -ne '1.932' -or $manifest.release_channel -ne 'stable') {
        throw 'The preview port is occupied by another server.'
    }
    Write-Host "Private preview already running at http://127.0.0.1:$Port/"
    exit 0
} catch {
    if ($_.Exception.Message -eq 'The preview port is occupied by another server.') { throw }
}
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
try { $listener.Start() } finally { $listener.Stop() }
$process = Start-Process -FilePath $python.Source -WindowStyle Hidden -PassThru `
    -ArgumentList @('-m', 'http.server', "$Port", '--bind', '127.0.0.1', '--directory', ('"' + $webRoot + '"')) `
    -RedirectStandardOutput (Join-Path $privateRoot 'preview.stdout.log') `
    -RedirectStandardError (Join-Path $privateRoot 'preview.stderr.log')
[IO.File]::WriteAllText((Join-Path $privateRoot 'preview.pid'), "$($process.Id)")
Write-Host "Private preview started at http://127.0.0.1:$Port/ (PID $($process.Id))."
Write-Host 'It accepts connections only from this computer. It does not launch the game.'
