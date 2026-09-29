param(
    [string]$Root = "C:\Crash Team Racing\CTR v2.0.0 Source",
    [string]$Stable = "C:\Crash Team Racing\CTR v152 STABLE Source 20260928"
)

$failed = 0

function Check-Pattern([string]$Name, [string]$File, [string]$Pattern) {
    $path = Join-Path $Root $File
    $ok = (Test-Path -LiteralPath $path) -and
          (Select-String -LiteralPath $path -Pattern $Pattern -Quiet)
    if ($ok) { Write-Host "[OK]   $Name" }
    else { Write-Host "[FAIL] $Name"; $script:failed++ }
}

function Check-Same([string]$File) {
    $a = Join-Path $Root $File
    $b = Join-Path $Stable $File
    $ok = (Test-Path -LiteralPath $a) -and
          (Test-Path -LiteralPath $b) -and
          ((Get-FileHash -Algorithm SHA256 -LiteralPath $a).Hash -eq
           (Get-FileHash -Algorithm SHA256 -LiteralPath $b).Hash)
    if ($ok) { Write-Host "[OK]   v152 intact: $File" }
    else { Write-Host "[FAIL] v152 changed unexpectedly: $File"; $script:failed++ }
}

Write-Host "CTR v2.0.8 character-mod regression checks"
Check-Pattern "dynamic registry" "game\CharacterRegistry.c" "Registry_DiscoverMods"
Check-Pattern "dynamic page count" "game\230\MM_Characters.c" "MM_Characters_RosterGetPageCount"
Check-Pattern "OBJ loader" "platform\native_obj.c" "NativeObj_LoadRacer"
Check-Pattern "OBJ renderer" "game\RenderBucket\RenderBucket_QueueExecute.c" "RenderBucket_DrawObj"
Check-Pattern "wheel update" "game\MAIN\MainFrame.c" "NativeObj_UpdateWheels"
Check-Pattern "safe restored mod IDs" "game\230\MM_Characters.c" "CharacterRegistry_GetByID\(\(int\)\*currID\)"
Check-Pattern "2P page restore" "game\230\MM_Characters.c" "firstVisibleIndex / MM_CHARACTER_SELECT_ROSTER_PAGE_SIZE"
Check-Pattern "podium fallback" "game\Podium.c" "CharacterRegistry_GetDriverPackID\(characterID\)"
Check-Pattern "weapon mask fallback" "game\UI\UI_Weapon.c" "CharacterRegistry_GetDriverPackID\(characterID\)"
Check-Pattern "voice fallback" "game\HOWL\HOWL_Voiceline.c" "CharacterRegistry_GetDriverPackID"
Check-Pattern "mod cleanup" "platform\native_platform.c" "NativeObj_Shutdown\(\)"
Check-Pattern "registry cleanup" "platform\native_platform.c" "CharacterRegistry_Shutdown\(\)"
Check-Pattern "native asset keys up to 63 chars" "platform\native_obj.c" "NativeObjCache \{ char name\[64\]"
Check-Pattern "OBJ mesh lookup uses descriptor identity" "platform\native_obj.c" "&c->descriptor->model == model"
Check-Pattern "subpixel 1/256" "platform\native_renderer.c" "1\.0 / 256\.0"
Check-Pattern "perspective depth" "platform\native_renderer.c" "a_depth"
Check-Pattern "PrimMem PC headroom" "game\MAIN\MainInit.c" "size \*= 4"
Check-Pattern "precise level culling" "platform\native_gpu.c" "NativeGpu_CorrectSubpixelNclip"

Check-Same "platform\native_gpu.c"
Check-Same "platform\native_gte_core.c"
Check-Same "game\LOAD\LOAD_Hub.c"
Check-Pattern "v152 level culling call intact" "game\226\226_00_DrawLevelOvr1P.c" "NativeGpu_CorrectSubpixelNclip"
$dangerous = @(
    @{File="game"; Pattern="MetaDataCharacters\s*\[\s*data\.characterIDs"; Name="direct metadata index"},
    @{File="game"; Pattern="BI_[A-Za-z0-9_]+\s*\+\s*data\.characterIDs"; Name="direct retail pack index"}
)

foreach ($check in $dangerous) {
    $matches = Get-ChildItem -LiteralPath (Join-Path $Root $check.File) -Recurse -File -Include *.c |
        Select-String -Pattern $check.Pattern
    if ($matches) {
        Write-Host "[FAIL] dangerous $($check.Name)"
        $matches | ForEach-Object { Write-Host "       $($_.Path):$($_.LineNumber)" }
        $failed++
    } else {
        Write-Host "[OK]   no $($check.Name)"
    }
}

$exe = Join-Path $Root "build_v200\ctr_native.exe"
if (Test-Path -LiteralPath $exe) {
    $version = & $exe --version
    if ($version -match "CTR Native 2\.0\.8") { Write-Host "[OK]   executable reports 2.0.8" }
    else { Write-Host "[FAIL] executable version"; $failed++ }
} else {
    Write-Host "[FAIL] build_v200 executable missing"
    $failed++
}

Write-Host ""
Write-Host "Failures: $failed"
exit ([int]($failed -ne 0))
