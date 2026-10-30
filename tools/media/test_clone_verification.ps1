$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'CloneVerification.ps1')
$trueFlags=@('all_target_files_cold_sha256_verified','all_source_immutable_files_sha256_equal',
    'current_runtime_files_preserved_byte_for_byte','all_effective_file_and_directory_metadata_verified',
    'all_frozen_source_paths_present','all_current_target_paths_accounted','current_target_metadata_preserved',
    'boot_gap_preserved','mbr_bootstrap_preserved','fat_backup_bpbs_and_fsinfo_valid')
$fixture=[ordered]@{result='verified_reconciled';protocol='segam-clone-reconciliation-v1';source_writes=0;
    target_bytes=31914983424L;baseline_sha256='c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445';
    target_content_bytes_written=0;target_file_metadata_writes=0;all_source_files_equal_at_original_paths=$false;
    source_file_and_directory_metadata_all_equal=$false;
    source_lilos_manifest_sha256='fe6e36a43e31bbdaaab4e329f9551c364b4bae9e72bc19b7f16280c78c27453f';
    source_lilroms_manifest_sha256='55178fac0b8e7b164945b940057046f18d526fd3b3dc92fee3b9dbacd93dfa14';
    volumes=@([ordered]@{label='lilos';files=1869;source_equal_files=1853;runtime_differences=15;runtime_extras=1},
        [ordered]@{label='lilroms';files=33754;source_equal_files=33752;runtime_differences=1;runtime_extras=1});
    runtime_exceptions=@(1..18)}
foreach($flag in $trueFlags){$fixture[$flag]=$true}
function Fresh { return ($fixture | ConvertTo-Json -Depth 8 | ConvertFrom-Json) }
function Rejected([scriptblock]$Code) {
    $failed=$false
    try{& $Code}catch{$failed=$true}
    if(!$failed){throw 'Invalid clone proof was accepted'}
    $script:checks++
}
$checks=0
Assert-CloneVerificationFields (Fresh);$checks++
foreach($flag in $trueFlags){
    $bad=Fresh;$bad.$flag=$false
    Rejected { Assert-CloneVerificationFields $bad }
    $bad=Fresh;$bad.$flag='true'
    Rejected { Assert-CloneVerificationFields $bad }
}
$bad=Fresh;$bad.source_writes=1;Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.target_bytes=62534975488L;Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.result='staged';Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.protocol='other';Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.target_content_bytes_written=1;Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.volumes[1].files=33753;Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.volumes[1].label='lilos';Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.runtime_exceptions=@(1..17);Rejected { Assert-CloneVerificationFields $bad }
$bad=Fresh;$bad.all_source_files_equal_at_original_paths=$true;Rejected { Assert-CloneVerificationFields $bad }
$legacy=Fresh;$legacy.result='verified';$legacy | Add-Member all_files_sha256_verified $true;$legacy | Add-Member source_cache_verified $true
Assert-CloneVerificationFields $legacy;$checks++
$legacy.all_files_sha256_verified=$false;Rejected { Assert-CloneVerificationFields $legacy }
"PASS $checks clone proof field tests; no SD access or writes"
