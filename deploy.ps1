# PowerShell Deployment & Execution Helper for WSL2
param(
    [string]$Distro = "Ubuntu-26.04",
    [switch]$RunTests,
    [switch]$Start
)

$WinPath = (Get-Item .).FullName
$DriveLetter = $WinPath.Substring(0,1).ToLower()
$WslMount = "/mnt/" + $DriveLetter + "/" + $WinPath.Substring(3).Replace('\', '/')

Write-Host ">>> Executing build and test via WSL ($Distro)..." -ForegroundColor Cyan
wsl -d $Distro -- bash "$WslMount/build_and_test.sh"

if ($Start) {
    Write-Host ">>> Launching Supervisor Daemon..." -ForegroundColor Magenta
    wsl -d $Distro bash -c "cd ~/tanmay && ./build/supervisor --workers 2 --heap-mb 512"
}
