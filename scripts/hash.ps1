# Use the .NET framework directly. A launcher can inherit a PowerShell
# module path from another shell, but checksum validation must not depend
# on module autoloading or on any third-party command.
function Get-Yw2Sha256([string]$Path) {
    $absolute=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $stream=[IO.File]::OpenRead($absolute)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
    finally { $sha.Dispose();$stream.Dispose() }
}
