$versionFile = "D:\obs-hider\CMakeLists.txt"
$content = Get-Content $versionFile -Raw
if ($content -match 'VERSION\s+(\d+)\.(\d+)\.(\d+)') {
    $major = [int]$matches[1]
    $minor = [int]$matches[2]
    $patch = [int]$matches[3]
    
    $minor++
    
    $newVersion = "$major.$minor.$patch"
    $newContent = $content -replace 'VERSION\s+\d+\.\d+\.\d+', "VERSION $newVersion"
    Set-Content $versionFile -Value $newContent
    Write-Host "Bumped version to $newVersion"
} else {
    Write-Host "Could not find version in $versionFile"
}
