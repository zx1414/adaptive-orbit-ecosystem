$ErrorActionPreference='Stop'
. "$PSScriptRoot\lib_sim.ps1"
$repo = Get-RepoRoot
Write-Host "repo=$repo exe=$(Get-QaSimExe)"
$stage = New-QaStage -StageDir (Join-Path $repo '_agent\red_play\stage') -Mods @()
Write-Host "stage=$stage"
$logDir = Join-Path $repo 'tests\artifacts\_smoke'
$srv = Start-QaServer -StageDir $stage -Port 19101 -LogDir $logDir -Tag 'smoke'
try {
  $envj = Get-EnvJson -Port $srv.Port
  Write-Host ("env keys: {0}" -f (($envj.PSObject.Properties.Name) -join ','))
  Write-Host ("axes={0} events={1} presets={2} influence={3}" -f $envj.axes.Count, $envj.events.Count, $envj.presets.Count, $envj.influence.cur)
  $rr = Invoke-SimHttp -Port $srv.Port -Method POST -Path '/run' -Body "seed=42`nballs=200`nnuclei=12`nframes=100000`nrender_interval=1`nsample_interval=0`ncyano_nodes=0`nmax_nuclei=0"
  Write-Host "run status=$($rr.Status) text=$($rr.Text)"
  Start-Sleep -Milliseconds 400
  $f0 = Pause-Sim -Port $srv.Port
  Write-Host "paused at frame=$f0 status=$(Get-SimStatus -Port $srv.Port)"
  $st = Get-SimState -Port $srv.Port
  Write-Host ("state frame={0} balls={1} alive={2} cyanoNodes={3} nucleus0 energy={4} id={5}" -f $st.Frame,$st.Balls,$st.Alive,$st.CyanoNodes,$st.Nuclei[0].Energy,$st.Nuclei[0].Id)
  $sv = Send-Save -Port $srv.Port -Name 'smoke1'
  Write-Host "save resp=$($sv.Text)"
  $savePath = Join-Path $stage 'saves\smoke1\save.bin'
  $sb = Read-SaveBin -Path $savePath
  Write-Host ("save version={0} frame={1} balls={2} nuclei={3} v2off={4} influence={5}" -f $sb.Version,$sb.Frame,$sb.BallCount,$sb.Nuclei.Count,$sb.V2Offset,$sb.Env.Influence)
  $step0 = Get-Date
  Step-Sim -Port $srv.Port -Count 120 -TargetFrame ($f0+120)
  Write-Host ("stepped to frame {0} in {1:N2}s" -f (Get-FrameNow -Port $srv.Port), ((Get-Date)-$step0).TotalSeconds)
} finally {
  Stop-QaServer -Server $srv
}
Write-Host "DONE"
