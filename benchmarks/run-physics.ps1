param(
  [string]$Executable = "$PSScriptRoot/../build/physics-release/bin/JadePhysicsBenchmark.exe",
  [string]$Output = "$PSScriptRoot/../build/physics-release/validation",
  [ValidateSet('scaling','workloads','extra','all')][string]$Suite = 'all'
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
[IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($Output)) | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
function Measure-Physics([string]$name,[string[]]$arguments) {
  $csv = Join-Path $Output "$name.csv"
  & $Executable @arguments --output $csv 2>&1 | Set-Content -LiteralPath (Join-Path $Output "$name.log")
  if ($LASTEXITCODE -ne 0) { throw "Physics benchmark failed: $name" }
  Import-Csv -LiteralPath $csv | Select-Object scene,count,workers,p50_ms,p95_ms,active,sleeping,participating,effective_worker_cores | ConvertTo-Json -Compress
}
if ($Suite -in 'scaling','all') {
  foreach ($workers in 0,1,3,6,12,23) {
    Measure-Physics "scene5-$workers" @('--scene','scene5','--count','100000','--workers',"$workers",'--warmup','4','--steps','24')
  }
}
if ($Suite -in 'workloads','all') {
  foreach ($scene in 'sparse','dense','rest','mixed','fields','ccd') {
    foreach ($count in 1000,10000,100000) {
      $warmup = if ($scene -eq 'rest') { '120' } else { '4' }
      Measure-Physics "$scene-$count-12" @('--scene',$scene,'--count',"$count",'--workers','12','--warmup',$warmup,'--steps','24')
    }
  }
}
if ($Suite -in 'extra','all') {
  Measure-Physics 'scene5-endurance-23' @('--scene','scene5','--count','100000','--workers','23','--warmup','4','--steps','240')
  Measure-Physics 'scene5-bridge-23' @('--scene','scene5','--count','100000','--workers','23','--warmup','4','--steps','24','--bridge','1')
  Measure-Physics 'scene5-ccd-12' @('--scene','scene5','--count','100000','--workers','12','--warmup','4','--steps','24','--continuous','1')
  Measure-Physics 'scene5-allocations-12' @('--scene','scene5','--count','100000','--workers','12','--warmup','4','--steps','12','--allocations','1')
  Measure-Physics 'rest-allocations-12' @('--scene','rest','--count','10000','--workers','12','--warmup','120','--steps','24','--allocations','1')
  Measure-Physics 'rest-awake-12' @('--scene','rest','--count','10000','--workers','12','--warmup','120','--steps','24','--sleep','0')
}
