$sourcePath = "glsl/"

$compilerExe = "glslangValidator.exe"
$optimizerExe = "spirv-opt.exe"
$optimizerConfig = "OptimizerConfig.cfg"
$declNames = @()
$declTimes = @()
foreach($srcFile in (Get-ChildItem $sourcePath -Recurse | Where-Object Extension -In '.decl'))
{
  $srcName = $srcFile.Name;
  $srcTime = $srcFile.LastWriteTime
  
  $declNames += $srcName
  $declTimes += $srcTime
}

foreach($srcFile in (Get-ChildItem $sourcePath -Recurse | Where-Object Extension -In '.frag','.vert','.comp'))
{
  $srcPath = $srcFile.fullName;
  $srcName = $srcFile.Name
  $dstPath = $srcPath.Replace("\glsl\", "\spirv\") + ".spv";
  $dstFolder = Split-Path $dstPath
  if(!(Test-Path $dstFolder))
  {
    New-Item -path $dstFolder -type directory
  }
  $needsRecompile = $false
  if(!(Test-Path $dstPath))
  {
    $needsRecompile = $true
    echo "$srcName is not compiled"
  }elseif(($srcFile.LastWriteTime -gt (Get-Item $dstPath).LastWriteTime))
  {
    echo "$srcName is old"
    $needsRecompile = $true
  }else
  {
    $srcContent = Get-Content $srcPath
    if($srcContent | Select-String "include")
    {
      for($i = 0; $i -lt $declNames.count; $i++)
      {
        if($srcContent | Select-String $declNames[$i])
        {
          if($declTimes[$i] -gt (Get-Item $dstPath).LastWriteTime)
          {
            $declName = $declNames[$i]
            echo "$srcName depends on $declName that's new"
            $needsRecompile = $true
          }
        }
      }
    }else
    {
      #echo NOINCLUDE
    }
  }
  if($needsRecompile)
  {
    #echo Building $srcPath
    Invoke-Expression "$compilerExe -V `"$srcPath`" -l --target-env vulkan1.1 -o `"$dstPath`""
  }
}

