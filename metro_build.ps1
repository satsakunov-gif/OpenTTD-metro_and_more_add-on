# Сборка OpenTTD Metro под Windows через MSYS2 (UCRT64).
# Использование:  powershell -ExecutionPolicy Bypass -File metro_build.ps1 [-Config RelWithDebInfo|Debug|Release] [-Clean]
# Требуется MSYS2 в D:\msys64 с пакетами (см. mod-docs/BUILD-WINDOWS.md).

param(
	[string]$Config = "RelWithDebInfo",
	[string]$Msys = "D:\msys64",
	[switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$build = Join-Path $root "build"
$bash = Join-Path $Msys "usr\bin\bash.exe"
if (-not (Test-Path $bash)) { throw "MSYS2 не найден: $bash" }

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
Push-Location $root
try {
	& $bash -lc "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build build"
	if ($LASTEXITCODE -ne 0) { throw "Сборка не удалась (код $LASTEXITCODE)" }

	# Копируем DLL из MSYS2 рядом с exe, чтобы игра запускалась двойным кликом.
	& $bash -lc "ldd build/openttd.exe | grep -i '/ucrt64/' | awk '{print `$3}' | xargs -r -I{} cp -u {} build/"

	# Базовая графика и звуки (OpenGFX / OpenSFX), если ещё не скачаны.
	$baseset = Join-Path $build "baseset"
	New-Item -ItemType Directory -Force $baseset | Out-Null
	$sets = @{
		"opengfx" = "https://cdn.openttd.org/opengfx-releases/7.1/opengfx-7.1-all.zip"
		"opensfx" = "https://cdn.openttd.org/opensfx-releases/1.0.3/opensfx-1.0.3-all.zip"
	}
	foreach ($name in $sets.Keys) {
		if (-not (Get-ChildItem $baseset -Filter "$name*" -ErrorAction SilentlyContinue)) {
			$zip = Join-Path $env:TEMP "$name.zip"
			Invoke-WebRequest $sets[$name] -OutFile $zip
			Expand-Archive $zip -DestinationPath $baseset -Force
			Remove-Item $zip
		}
	}
	Write-Host "Готово: $build\openttd.exe"
} finally {
	Pop-Location
}
