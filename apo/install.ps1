# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([switch]$VirtualMachine,[switch]$Rollback,[switch]$NoRestart)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Join-Path $env:ProgramFiles 'Lenovo14eAudio'
$backup=Join-Path $env:ProgramData 'Lenovo14eAudio\installation.json'
$clsid='{C324C041-34EC-4D9A-9CC8-B06514E00001}'
$efx='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7'
$association='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},0'
$modes='{d3993a3f-99c2-4402-b5ec-a92a0367664b},7'
$disable='{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5'
function Register-Apo([bool]$Remove) {
 $arguments=@('/s')
 if($Remove){$arguments+='/u'}
 $arguments+=('"{0}"' -f (Join-Path $root 'lenovo14eapo.dll'))
 $process=Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList $arguments -Wait -PassThru
 if($process.ExitCode -ne 0){throw "APO registration tool returned exit code $($process.ExitCode)."}
}
function Restart-Audio {
 if($NoRestart){return}
 Stop-Service Audiosrv -Force
 Restart-Service AudioEndpointBuilder -Force
 Start-Service Audiosrv
}
function Read-Value($path,$name) {
 $key=Get-Item $path -ErrorAction SilentlyContinue
 $exists=$key -and ($key.GetValueNames() -contains $name)
 if($exists){return @{Path=$path;Name=$name;Exists=$true;Kind=$key.GetValueKind($name).ToString();Value=$key.GetValue($name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}}
 return @{Path=$path;Name=$name;Exists=$false}
}
function Restore-Values($values) {
 foreach($v in $values){
  if($v.Exists){
   if(-not(Test-Path $v.Path)){New-Item $v.Path -Force | Out-Null}
   $value=$v.Value
   switch($v.Kind){ 'Binary' {$value=[byte[]]$value};'DWord' {$value=[int]$value};'QWord' {$value=[long]$value};'MultiString' {$value=[string[]]$value} }
   New-ItemProperty $v.Path -Name $v.Name -PropertyType $v.Kind -Value $value -Force | Out-Null
  }else{Remove-ItemProperty $v.Path -Name $v.Name -ErrorAction SilentlyContinue}
 }
}
$id=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this installer in an elevated PowerShell window.'}
# MMDevices effect stores are SYSTEM-owned. Use a short-lived installer service
# rather than changing ownership or permanently broadening their ACLs.
if($id.User.Value -ne 'S-1-5-18'){
 $stage=Join-Path $root 'Installer'
 New-Item $stage -ItemType Directory -Force | Out-Null
 foreach($name in @('install.ps1','lenovo14eapo.dll','lenovo14e-apo-probe.exe','lenovo14e-apo-worker.exe')){
  $source=Join-Path $PSScriptRoot $name;$dest=Join-Path $stage $name
  if([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($dest)){Copy-Item $source $dest -Force}
 }
 $flags=''
 if($VirtualMachine){$flags+=' -VirtualMachine'}
 if($Rollback){$flags+=' -Rollback'}
 if($NoRestart){$flags+=' -NoRestart'}
 $operation='Lenovo14eAudio-'+[Guid]::NewGuid().ToString('N')
 $log=Join-Path $stage ($operation+'.log')
 $runner=Join-Path $stage ($operation+'.ps1')
 $escaped=(Join-Path $stage 'install.ps1').Replace("'","''");$escapedLog=$log.Replace("'","''")
 $body="`$ErrorActionPreference='Stop'`ntry { & '$escaped'$flags *>&1 | Out-File '$escapedLog'; exit 0 } catch { `$_ | Out-String | Add-Content '$escapedLog'; exit 1 }"
 Set-Content $runner $body -Encoding UTF8
 & (Join-Path $stage 'lenovo14e-apo-worker.exe') --run $runner $operation
 $code=$LASTEXITCODE
 if(Test-Path $log){Get-Content $log}
 if($code -ne 0){throw "The SYSTEM installer failed ($code). See $log"}
 return
}
if($Rollback){
 if(-not(Test-Path $backup)){throw 'No recorded installation to roll back.'}
 $record=Get-Content $backup -Raw | ConvertFrom-Json
 $now=Get-ItemPropertyValue $record.FxPath -Name $efx -ErrorAction SilentlyContinue
 if($now -and $now -ne $clsid){throw 'Another effect replaced this APO. Refusing to overwrite it during rollback.'}
 Restore-Values $record.Values
 Register-Apo $true
 Restart-Audio
 Move-Item $backup ($backup+'.removed-'+(Get-Date -Format yyyyMMddHHmmss))
 Write-Output 'RESTORED: previous speaker effects. Kernel drivers were not changed.'
 return
}
if(Test-Path $backup){throw 'An installation is already recorded. Roll it back before replacing it.'}
$computer=Get-CimInstance Win32_ComputerSystem
if($VirtualMachine){
 if(($computer.Manufacturer+' '+$computer.Model) -notmatch 'QEMU|Bochs|Virtual Machine|VirtualBox|VMware'){throw 'The VM override is restricted to virtual machines.'}
}else{
 if($computer.Manufacturer -ne 'Google' -or $computer.Model -notmatch '^Pujjoteen'){throw 'This fork supports the Lenovo 14e Gen 3 / Google Pujjoteen only.'}
}
$render='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$targets=@(Get-ChildItem $render | Where-Object {
 $state=(Get-ItemProperty $_.PSPath).DeviceState
 $p=Get-ItemProperty ($_.PSPath+'\Properties')
 $adapter=$p.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
 $name=$p.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
 if($VirtualMachine){$state -eq 1 -and $name -match 'Speaker'}
 else{$state -eq 1 -and $adapter -match 'SOF.*speaker|Lenovo 14e.*Speaker'}
})
if($targets.Count -ne 1){throw "Expected one active supported speaker endpoint; found $($targets.Count)."}
$target=$targets[0];$fx=$target.PSPath+'\FxProperties';$props=$target.PSPath+'\Properties'
$values=@((Read-Value $fx $efx),(Read-Value $fx $modes),(Read-Value $props $disable),(Read-Value $fx $association))
# Refuse to silently replace an existing endpoint effect.
$prior=$values[0];if($prior.Exists -and $prior.Value -and $prior.Value -ne $clsid){throw "The speaker already has an endpoint APO: $($prior.Value)."}
New-Item $root -ItemType Directory -Force | Out-Null
New-Item (Split-Path $backup) -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'lenovo14eapo.dll') $root -Force
Copy-Item (Join-Path $PSScriptRoot 'lenovo14e-apo-probe.exe') $root -Force
$record=@{Endpoint=$target.PSChildName;FxPath=$fx;Values=$values;InstalledAt=(Get-Date).ToString('o');VirtualMachine=[bool]$VirtualMachine}
$record | ConvertTo-Json -Depth 8 | Set-Content $backup -Encoding UTF8
try {
 Register-Apo $false
 if(-not(Test-Path $fx)){New-Item $fx -Force | Out-Null}
 New-ItemProperty $fx -Name $association -Value '{00000000-0000-0000-0000-000000000000}' -PropertyType String -Force | Out-Null
 New-ItemProperty $fx -Name $efx -Value $clsid -PropertyType String -Force | Out-Null
 New-ItemProperty $fx -Name $modes -Value @('{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}') -PropertyType MultiString -Force | Out-Null
 New-ItemProperty $props -Name $disable -Value 0 -PropertyType DWord -Force | Out-Null
 Restart-Audio
 Write-Output ('INSTALLED: Lenovo 14e speaker APO on '+$target.PSChildName)
 Write-Output 'The kernel driver, amplifier watchdog, microphone and headphones were not modified.'
}catch{
 Restore-Values $values
 Register-Apo $true
 Remove-Item $backup -ErrorAction SilentlyContinue
 Restart-Audio
 throw
}
