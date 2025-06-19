@echo off

echo ====================================
echo == Checking for AssetCooker exec. ==
echo ====================================
echo(

IF NOT EXIST "tools/bin/AssetCooker.exe" (

    echo AssetCooker executable not found - downloading it now...

    pushd "tools/bin"
    powershell -command "Invoke-WebRequest -Uri 'https://github.com/jlaumon/AssetCooker/releases/download/v0.0.1/AssetCooker-v0.0.1.zip' -OutFile 'AssetCooker.zip'"
    powershell -command "Expand-Archive -Path AssetCooker.zip -DestinationPath ."
    popd

) ELSE (
    echo AssetCooker executable found!
)

echo(
echo ====================================
echo ====== Ensure tools are built ======
echo ====================================
echo(

cmake --build build --target GltfImportTool --config Release
cmake --build build --target IESConvertTool --config Release
cmake --build build --target ArkAssetBakeTool --config Release
cmake --build build --target ImgAssetBakeTool --config Release
cmake --build build --target CopyFileTool --config Release

IF NOT "%PXR_USD_LOCATION%"=="" (
    cmake --build build --target UsdImportTool --config Release
)

echo(
echo ====================================
echo ====== Launching AssetCooker! ======
echo ====================================

pushd "tools/bin"
start AssetCooker.exe
popd
