function Assert-CloneTrueFlag($Report,[string]$Name) {
    $property=$Report.PSObject.Properties[$Name]
    if($null -eq $property -or $property.Value -isnot [bool] -or !$property.Value){throw "Clone proof missing true flag: $Name"}
}
function Assert-CloneVerificationFields($Report) {
    if($Report.source_writes -ne 0 -or $Report.target_bytes -ne 31914983424 -or
       $Report.baseline_sha256 -ne 'c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445'){
        throw 'Clone geometry, baseline or source-write proof differs'
    }
    if($Report.result -eq 'verified'){
        Assert-CloneTrueFlag $Report 'all_files_sha256_verified'
        Assert-CloneTrueFlag $Report 'source_cache_verified'
        return
    }
    if($Report.result -ne 'verified_reconciled' -or $Report.protocol -ne 'segam-clone-reconciliation-v1'){
        throw 'Incomplete or unsupported clone proof'
    }
    foreach($name in @('all_target_files_cold_sha256_verified','all_source_immutable_files_sha256_equal',
        'current_runtime_files_preserved_byte_for_byte','all_effective_file_and_directory_metadata_verified',
        'all_frozen_source_paths_present','all_current_target_paths_accounted','current_target_metadata_preserved',
        'boot_gap_preserved','mbr_bootstrap_preserved','fat_backup_bpbs_and_fsinfo_valid')){
        Assert-CloneTrueFlag $Report $name
    }
    if($Report.all_source_files_equal_at_original_paths -ne $false -or
       $Report.source_file_and_directory_metadata_all_equal -ne $false -or
       $Report.target_content_bytes_written -ne 0 -or $Report.target_file_metadata_writes -ne 0 -or
       $Report.source_lilos_manifest_sha256 -ne 'fe6e36a43e31bbdaaab4e329f9551c364b4bae9e72bc19b7f16280c78c27453f' -or
       $Report.source_lilroms_manifest_sha256 -ne '55178fac0b8e7b164945b940057046f18d526fd3b3dc92fee3b9dbacd93dfa14'){
        throw 'Reconciliation content, metadata or source snapshot proof differs'
    }
    $expected=@{lilos=@(1869,1853,15,1);lilroms=@(33754,33752,1,1)}
    $seen=@{}
    foreach($volume in $Report.volumes){
        if(!$expected.ContainsKey($volume.label) -or $seen.ContainsKey($volume.label)){throw 'Unexpected/duplicate clone volume'}
        $want=$expected[$volume.label]
        if($volume.files -ne $want[0] -or $volume.source_equal_files -ne $want[1] -or
           $volume.runtime_differences -ne $want[2] -or $volume.runtime_extras -ne $want[3]){throw 'Reconciled volume counts differ'}
        $seen[$volume.label]=$true
    }
    if($seen.Count -ne 2 -or @($Report.runtime_exceptions).Count -ne 18){throw 'Incomplete reconciled coverage'}
}
function Read-CloneVerificationProof([string]$Project) {
    $Project=[IO.Path]::GetFullPath($Project)
    $evidence=Join-Path $Project 'build/card-clone-20261024'
    $serial=Join-Path $evidence 'serial-20261025'
    $path=Join-Path $evidence 'verification.json'
    $report=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    Assert-CloneVerificationFields $report
    $hash={param($p) (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
    $cachePath=Join-Path $serial 'cache-verification.json'
    $cacheHash=& $hash $cachePath
    if($cacheHash -ne '729785c7497393369298d1fd291ee028b719701db524cb5ed51633268a41bde0'){throw 'Sealed source cache proof changed'}
    if($report.result -eq 'verified'){
        if($report.cache_verification_sha256 -ne $cacheHash){throw 'Legacy clone/cache proof differs'}
    }else{
        if($report.source_cache_verification_sha256 -ne $cacheHash){throw 'Reconciled clone/cache proof differs'}
        $root=Join-Path $serial 'target-reconcile-20261025-1'
        $snapshotPath=Join-Path $root 'snapshot-completed.json'
        if((& $hash $snapshotPath) -ne $report.snapshot_report_sha256){throw 'Runtime snapshot proof changed'}
        $snapshot=Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
        $snapshotNonce=$report.launch_nonce
        if($report.PSObject.Properties['original_snapshot_launch_nonce']){$snapshotNonce=$report.original_snapshot_launch_nonce}
        if($snapshot.result -ne 'snapshotted' -or $snapshot.launch_nonce -cne $snapshotNonce -or
           $snapshot.hash_audit_sha256 -ne $report.pre_reconcile_hash_audit_sha256 -or
           @($snapshot.current_runtime_snapshots).Count -ne 18){throw 'Runtime snapshot provenance differs'}
        if($report.PSObject.Properties['verification_action'] -and $report.verification_action -eq 'VerifyResume'){
            $previousPath=Join-Path $root 'all-reconcile-state.json'
            if((& $hash $previousPath) -ne $report.verification_resume_parent_guard_sha256){throw 'Resumed verifier guard changed'}
            $previous=Get-Content -LiteralPath $previousPath -Raw | ConvertFrom-Json
            if($previous.state -ne 'failed' -or $previous.launch_nonce -cne $snapshotNonce){throw 'Resume does not reference the preserved failed attempt'}
        }
        $unique=@{}
        foreach($file in $report.runtime_exceptions){
            $key=$file.label+'|'+$file.path
            if($unique.ContainsKey($key)){throw 'Duplicate preserved runtime file'}
            $unique[$key]=$true
            $backup=[IO.Path]::GetFullPath($file.backup)
            if(!$backup.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or
               (Get-Item -LiteralPath $backup).Length -ne $file.bytes -or (& $hash $backup) -ne $file.sha256){
                throw 'Preserved runtime backup differs or escapes evidence directory'
            }
            $matching=@($snapshot.current_runtime_snapshots | Where-Object { $_.label -ceq $file.label -and $_.path -ceq $file.path })
            if($matching.Count -ne 1 -or $matching[0].sha256 -ne $file.sha256 -or $matching[0].bytes -ne $file.bytes){throw 'Runtime exception differs from snapshot'}
        }
        foreach($volume in $report.volumes){
            $cold=Join-Path $root ('verifyreconciled-'+$volume.label+'-sha256.jsonl')
            if((& $hash $cold) -ne $volume.cold_sha256_ledger_sha256){throw 'Cold target hash ledger changed'}
            $effective=@($report.effective_entry_ledgers | Where-Object label -CEQ $volume.label)
            if($effective.Count -ne 1 -or (& $hash (Join-Path $root ($volume.label+'-effective-entries.jsonl'))) -ne $effective[0].sha256){throw 'Effective target ledger changed'}
        }
    }
    return [pscustomobject]@{Report=$report;Sha256=(& $hash $path);CacheSha256=$cacheHash;RuntimePreserved=($report.result -eq 'verified_reconciled')}
}
