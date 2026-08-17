# Vendors the ARCore C API for Android: the single-header C API
# (arcore_c_api.h, from the public arcore-android-sdk GitHub repo) plus
# libarcore_sdk_c.so (from the com.google.ar:core Maven AAR, arm64-v8a
# slice only -- matches this project's sole supported ABI, see
# mobile/export_presets.cfg architectures/*). The .so here is for the
# native C++ LINK step only (mobile/native/SConstruct's LIBPATH/LIBS) --
# the copy that actually ships in the APK, alongside ARCore's Java classes
# (required: ArCoreApk_checkAvailabilityAsync calls back into them via JNI
# and aborts under -Xcheck:jni without them, confirmed on-device), comes
# from the real Gradle dependency in
# mobile/android/plugins/jni_bootstrap/build.gradle.kts
# ("com.google.ar:core"), not from this script.
#
# Output goes to mobile/thirdparty/arcore/, which is .gitignored: ARCore's
# SDK carries Google's own "ARCore Additional Terms of Service" (see the
# header's own copyright banner), not this project's license, so it is not
# committed -- every clone/CI run must run this script once before building
# the android_context/ARCore native sources (see docs/ARCHITECTURE.md).
#
# Usage: pwsh mobile/thirdparty/fetch_arcore_sdk.ps1 [-Version 1.54.0]

param(
    [string]$Version = "1.54.0"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutDir = Join-Path $ScriptDir "arcore"
$IncludeDir = Join-Path $OutDir "include"
$JniDir = Join-Path $OutDir "jni\arm64-v8a"
$TmpDir = Join-Path $OutDir ".tmp"

New-Item -ItemType Directory -Force -Path $IncludeDir | Out-Null
New-Item -ItemType Directory -Force -Path $JniDir | Out-Null
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null

Write-Host "Fetching arcore_c_api.h header..."
$HeaderUrl = "https://raw.githubusercontent.com/google-ar/arcore-android-sdk/master/libraries/include/arcore_c_api.h"
Invoke-WebRequest -Uri $HeaderUrl -OutFile (Join-Path $IncludeDir "arcore_c_api.h")

Write-Host "Fetching core-$Version.aar from Maven..."
$AarUrl = "https://dl.google.com/dl/android/maven2/com/google/ar/core/$Version/core-$Version.aar"
$AarPath = Join-Path $TmpDir "core-$Version.aar"
Invoke-WebRequest -Uri $AarUrl -OutFile $AarPath

Write-Host "Extracting libarcore_sdk_c.so (arm64-v8a)..."
$ZipPath = Join-Path $TmpDir "core-$Version.zip"
Copy-Item $AarPath $ZipPath -Force
Expand-Archive -Path $ZipPath -DestinationPath $TmpDir -Force
Copy-Item (Join-Path $TmpDir "jni\arm64-v8a\libarcore_sdk_c.so") (Join-Path $JniDir "libarcore_sdk_c.so") -Force

Remove-Item -Recurse -Force $TmpDir

Write-Host "Done. Vendored to $OutDir"
Write-Host "  include/arcore_c_api.h"
Write-Host "  jni/arm64-v8a/libarcore_sdk_c.so (link-time only; the APK's runtime copy + Java classes come from the Gradle 'com.google.ar:core' dependency)"
