# Filter zones by formation_datetime
param(
    [datetime]$StartDate = [datetime]"2025-01-01",
    [datetime]$EndDate = [datetime]::MaxValue,
    [string]$OutputFile = "filtered_zones.json",
    [switch]$Before  # If specified, gets zones BEFORE the start date instead
)

$sourceFile = "D:\SD_System\SD_Trading_V4_Dupe_Zones_Fix\results\live_trades\zones_live_master.json"

Write-Host "=== ZONE FILTER TOOL ===" -ForegroundColor Cyan
Write-Host "Source: $sourceFile" -ForegroundColor Yellow

$masterJson = Get-Content $sourceFile | ConvertFrom-Json

Write-Host "Total zones in master: $($masterJson.zones.Count)" -ForegroundColor White

# Filter zones
$filteredZones = @()

foreach ($zone in $masterJson.zones) {
    $zoneDate = [datetime]::ParseExact($zone.formation_datetime, 'yyyy-MM-dd HH:mm:ss', $null)
    
    $include = $false
    
    if ($Before) {
        # Include zones BEFORE start date
        $include = $zoneDate -lt $StartDate
    } else {
        # Include zones between start and end date
        $include = ($zoneDate -ge $StartDate) -and ($zoneDate -le $EndDate)
    }
    
    if ($include) {
        $filteredZones += $zone
    }
}

Write-Host "Filtered zones: $($filteredZones.Count)" -ForegroundColor Green

# Create output JSON
$outputJson = $masterJson
$outputJson.zones = $filteredZones

$outputJson | ConvertTo-Json -Depth 100 | Out-File -FilePath $OutputFile -Encoding UTF8

Write-Host "Output: $OutputFile" -ForegroundColor Yellow

# Show summary
Write-Host "`n=== SUMMARY ===" -ForegroundColor Cyan
if ($filteredZones.Count -gt 0) {
    $dates = $filteredZones | ForEach-Object { [datetime]::ParseExact($_.formation_datetime, 'yyyy-MM-dd HH:mm:ss', $null) }
    $minDate = ($dates | Measure-Object -Minimum).Minimum
    $maxDate = ($dates | Measure-Object -Maximum).Maximum
    
    Write-Host "Date range: $($minDate | Get-Date -Format 'MMM dd, yyyy') to $($maxDate | Get-Date -Format 'MMM dd, yyyy')" -ForegroundColor White
    
    $typeGroups = $filteredZones | Group-Object type
    Write-Host "Zone types:" -ForegroundColor White
    $typeGroups | ForEach-Object { Write-Host "  $($_.Name): $($_.Count)" -ForegroundColor Gray }
    
    $stateGroups = $filteredZones | Group-Object state
    Write-Host "Zone states:" -ForegroundColor White
    $stateGroups | ForEach-Object { Write-Host "  $($_.Name): $($_.Count)" -ForegroundColor Gray }
}
