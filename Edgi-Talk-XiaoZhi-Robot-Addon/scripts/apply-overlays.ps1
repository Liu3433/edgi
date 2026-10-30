<#
Copies the project-specific overlay into matching official SDK workspaces.
It intentionally does not copy the complete vendor SDK or any build output.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $M55Project,
    [Parameter(Mandatory = $true)] [string] $M33Project
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$M55Project = (Resolve-Path -LiteralPath $M55Project).Path
$M33Project = (Resolve-Path -LiteralPath $M33Project).Path

foreach ($required in @(
    (Join-Path $M55Project 'applications\\xiaozhi'),
    (Join-Path $M33Project 'applications')
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "不是预期的 Edgi-Talk SDK 工程：$required"
    }
}

function Copy-Overlay([string] $overlay, [string] $target) {
    Get-ChildItem -LiteralPath $overlay -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $target $_.Name) -Recurse -Force
    }
}

Copy-Overlay (Join-Path $repoRoot 'm55-overlay') $M55Project
Copy-Overlay (Join-Path $repoRoot 'm33-overlay') $M33Project

Write-Host '覆盖文件已复制。'
Write-Host '请在 M55 工程根目录应用 patches\\m55-websocket-fin.patch，然后重新生成配置并编译 M33、M55。'
