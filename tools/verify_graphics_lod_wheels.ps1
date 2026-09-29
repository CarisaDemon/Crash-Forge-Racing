param(
    [string]$Root = "C:\Crash Team Racing\CTR v2.0.0 Source"
)

$fail = 0
function Check([string]$Label, [string]$Rel, [string]$Pattern) {
    $path = Join-Path $Root $Rel
    if ((Test-Path -LiteralPath $path) -and (Select-String -LiteralPath $path -Pattern $Pattern -Quiet)) {
        Write-Host "[OK]   $Label"
    } else {
        Write-Host "[FAIL] $Label"
        $script:fail++
    }
}

Check "character detail setting persisted" "platform\native_platform.c" "character_detail=%d"
Check "character detail mode API" "platform\native_platform.c" "Platform_GetCharacterDetailMode"
Check "advanced menu detail row" "game\MAIN\MainFreeze.c" "CHARACTER DETAIL"
Check "LOW detail pins furthest racer header" "game\RenderBucket\RenderBucket_QueueExecute.c" "return lastMh"
Check "HIGH pins first racer header" "game\RenderBucket\RenderBucket_QueueExecute.c" "detailMode == 2"
Check "HIGH keeps final vanilla visibility cutoff" "game\RenderBucket\RenderBucket_QueueExecute.c" "lastMh->maxDistanceLOD"
Check "ULTRA pins first racer header without header-count guard" "game\RenderBucket\RenderBucket_QueueExecute.c" "detailMode == 3"
Check "ULTRA racer bypasses distance LOD mask" "game\RenderBucket\RenderBucket_QueueExecute.c" "Platform_GetCharacterDetailMode\(\) == 3"
Check "ULTRA particles extend depth with bounded cull" "game\Particle.c" "ultraDepthLimit = particleDepthLimit \* 4"
Check "native water animates once per retail tick" "game\RenderLevel\AnimateWater.c" "s_nativeWaterTimer == timer"
Check "native water restores retail subdivision distance" "game\226\226_00_DrawLevelOvr1P.c" "threshold >>= 1"
Check "water perf bucket" "include\platform\native_perf.h" "NATIVE_PERF_BUCKET_WATER_RENDER"
Check "particle perf bucket" "include\platform\native_perf.h" "NATIVE_PERF_BUCKET_PARTICLE_RENDER"
Check "native OBJ remains single-LOD independent" "game\RenderBucket\RenderBucket_QueueExecute.c" "mh->unk1 == NATIVE_OBJ_MODEL_MAGIC"
Check "OBJ subpixel cull correction" "game\RenderBucket\RenderBucket_QueueExecute.c" "NativeGpu_CorrectSubpixelNclip"
Check "OBJ wheel steering state" "platform\native_obj.c" "steerAngle"
Check "OBJ front wheels steer in renderer" "game\RenderBucket\RenderBucket_QueueExecute.c" "w < 2"
Check "wheel steering uses retail wheelRotation" "platform\native_obj.c" "d->wheelRotation"
Check "retail 3D wheel override loader" "platform\native_obj.c" "NativeObj_GetRetailWheelOverrideMesh"
Check "retail 3D wheel replacement gate" "platform\native_obj.c" "NativeObj_ShouldReplaceRetailWheels"
Check "retail 3D wheel mesh renderer" "game\DrawTires.c" "DrawTires3D_RenderOverride"
Check "retail 2D reflection suppressed under 3D override" "game\DrawTires.c" "solid pass already drew the imported 3D wheel mesh"
Check "retail 3D wheels detected by rendered model" "platform\native_obj.c" "Detect a retail kart by the model actually being rendered"
Check "3D wheel renderer requires emitted triangles" "game\DrawTires.c" "return emitted > 0"
Check "3D wheel draw diagnostic" "game\DrawTires.c" "3D wheels: emitted"
Check "3D wheels use real kart axes" "game\DrawTires.c" "actual kart matrix"
Check "3D wheels preserve authored width" "game\DrawTires.c" "preserving the imported tire's real width"
Check "high refresh pacing does not catch up missed deadlines" "platform\native_platform.c" "missed presentation deadlines"

Write-Host ""
Write-Host "Failures: $fail"
exit $fail
