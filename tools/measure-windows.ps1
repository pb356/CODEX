param(
 [Parameter(Mandatory=$true)][int]$AppProcessId,
 [Parameter(Mandatory=$true)][ValidateSet('effects-off','hover-playing','fade-finished','wallpaper-off','wallpaper-energy','wallpaper-standard','wallpaper-with-hover','wallpaper-paused','large-list','soak')][string]$Phase,
 [ValidateRange(10,86400)][int]$Seconds = 600,
 [ValidateRange(1,60)][int]$Interval = 5,
 [string]$OutputDirectory = (Join-Path $env:TEMP 'DesktopBoxesMeasurements')
)
$ErrorActionPreference = 'Stop'
# Only samples the selected process and GPU counters. Never generates mouse input.
# Set the actual effect/hover state manually BEFORE starting each separate phase.
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$logical = [Environment]::ProcessorCount
$system = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors
$start = Get-Date
$stamp = $start.ToString('yyyyMMdd-HHmmss')
$rows = [Collections.Generic.List[object]]::new()
$p = Get-Process -Id $AppProcessId
$prevTime = Get-Date
$prevCpu = $p.TotalProcessorTime.TotalSeconds
$initialCpu = $prevCpu
$initialSampleTime = $prevTime
$gpuAvailable = $true
$gpuPaths = @()
try {
  $gpuPaths = @((Get-Counter -ListSet 'GPU Engine').PathsWithInstances | Where-Object { $_ -like "*pid_${AppProcessId}_*\Utilization Percentage" })
  if ($gpuPaths.Count -eq 0) { $gpuAvailable = $false }
} catch { $gpuAvailable = $false }
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
 Start-Sleep -Seconds $Interval
 $p = Get-Process -Id $AppProcessId
 $now = Get-Date
 $elapsed = ($now - $prevTime).TotalSeconds
 $procCpu = $p.TotalProcessorTime.TotalSeconds
 $gpuMax = $null
 if ($gpuAvailable) {
   try { $gpuMax = (Get-Counter -Counter $gpuPaths -ErrorAction Stop).CounterSamples | Measure-Object -Property CookedValue -Maximum | Select-Object -ExpandProperty Maximum }
   catch { $gpuAvailable = $false }
 }
 $rows.Add([pscustomobject]@{
  Time=$now.ToString('o'); Phase=$Phase
  CpuWholeMachinePercent=100 * ($procCpu-$prevCpu) / $elapsed / $logical
  PrivateMiB=$p.PrivateMemorySize64/1MB; WorkingSetMiB=$p.WorkingSet64/1MB
  Threads=$p.Threads.Count; Handles=$p.HandleCount
  GpuMaxEnginePercent=$gpuMax
 })
 $prevTime=$now; $prevCpu=$procCpu
}
$csv = Join-Path $OutputDirectory "$stamp-$Phase.csv"
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 $csv
$summary = [ordered]@{
 Phase=$Phase; Started=$start.ToString('o'); SampleCount=$rows.Count
 ActualSeconds=((Get-Date)-$start).TotalSeconds; IntervalSeconds=$Interval
 OS=$system.Caption; Version=$system.Version; Build=$system.BuildNumber
 RamGiB=[math]::Round($system.TotalVisibleMemorySize*1KB/1GB,2); Cpu=$cpu
 LogicalProcessors=$logical
 CpuMeanWholeMachinePercent=100 * ($prevCpu-$initialCpu) / ($prevTime-$initialSampleTime).TotalSeconds / $logical
 PrivateMeanMiB=($rows | Measure-Object PrivateMiB -Average).Average
 PrivateSamplePeakMiB=($rows | Measure-Object PrivateMiB -Maximum).Maximum
 PrivateFirstMiB=$rows[0].PrivateMiB; PrivateLastMiB=$rows[$rows.Count-1].PrivateMiB
 ThreadsFirst=$rows[0].Threads; ThreadsLast=$rows[$rows.Count-1].Threads
 HandlesFirst=$rows[0].Handles; HandlesLast=$rows[$rows.Count-1].Handles
 GpuCounterAvailable=$gpuAvailable
 GpuMeaning='Maximum utilization of one engine attributed to selected process; not summed, excludes DWM; null means unmeasured.'
 Notes='Report display resolution/scaling, SSD, effect preset, number of boxes/items, startup and response latency separately. Samples cannot prove sub-sample peaks or absence of leaks.'
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory "$stamp-$Phase-summary.json")
$summary | ConvertTo-Json -Depth 6
