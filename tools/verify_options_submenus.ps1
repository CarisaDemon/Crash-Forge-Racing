param([string]$Root = "C:\Crash Team Racing\CTR v2.0.0 Source")
$fail = 0
function Check([string]$label,[string]$rel,[string]$pattern) {
  $p = Join-Path $Root $rel
  if ((Test-Path -LiteralPath $p) -and (Select-String -LiteralPath $p -Pattern $pattern -Quiet)) {
    Write-Host "[OK]   $label"
  } else {
    Write-Host "[FAIL] $label"
    $script:fail++
  }
}
Check "root AUDIO category" "game\MAIN\MainFreeze.c" '"AUDIO"'
Check "root GRAPHICS category" "game\MAIN\MainFreeze.c" '"GRAPHICS"'
Check "root CONTROLS category" "game\MAIN\MainFreeze.c" '"CONTROLS"'
Check "audio page" "game\MAIN\MainFreeze.c" 'MainFreeze_NativeOptionsDrawAudio'
Check "graphics page" "game\MAIN\MainFreeze.c" 'MainFreeze_NativeOptionsDrawGraphics'
Check "controls page" "game\MAIN\MainFreeze.c" 'MainFreeze_NativeOptionsDrawControls'
Check "CTR transition engine" "game\MAIN\MainFreeze.c" 'MM_TransitionInOut\(s_nativeOptionsSlideMeta'
Check "CTR swoosh duration path" "game\MAIN\MainFreeze.c" 'NATIVE_OPTIONS_TRANSITION_FRAMES'
Check "title and pause share native options" "game\MAIN\MainFreeze.c" 'MainFreeze_NativeOptionsUpdate\(menu, &gamepad\)'
Check "graphics character detail" "game\MAIN\MainFreeze.c" 'Platform_GetCharacterDetailMode'
Check "audio persistent volume" "game\MAIN\MainFreeze.c" 'Platform_SaveAudioVolume'
Check "controls vibration persistence" "game\MAIN\MainFreeze.c" 'Platform_SaveVibrationMask'
Check "title EXIT row" "game\230\MM_MenuFlow.c" 'LNG_OPTIONS_EXIT'
Check "title EXIT opens confirmation" "game\230\MM_MenuFlow.c" 'MM_NativeTitleExitConfirmOpen'
Check "title EXIT confirmation is English" "game\230\MM_MenuFlow.c" 'ARE YOU SURE YOU WANT TO EXIT'
Check "title EXIT confirmation YES/NO" "game\230\MM_MenuFlow.c" '"YES"'
Check "title EXIT confirmation slides" "game\230\MM_MenuFlow.c" 'MM_TransitionInOut\(s_titleExitConfirmMeta'
Check "title EXIT uses transition route after YES" "game\230\MM_MenuFlow.c" 'MM_EXIT_ROUTE_APPLICATION'
Check "title EXIT shuts down after transition" "game\230\MM_Title.c" 'Platform_RequestExit'
Check "Options initial entry slides" "game\MAIN\MainFreeze.c" 's_nativeOptionsEntryActive'
Check "Options exit slides" "game\MAIN\MainFreeze.c" 's_nativeOptionsExitActive'
Write-Host ""
Write-Host "Failures: $fail"
exit $fail
