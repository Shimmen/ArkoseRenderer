@echo off

echo ====================================
echo == Checking for AssetCooker exec. ==
echo ====================================
echo(

IF NOT EXIST "tools/bin/AssetCooker.exe" (
    echo ERROR: AssetCooker executable not found!
    echo(
    echo Please download, build, and copy the executable into this directory! This is temporary..
    echo You can find the code at https://github.com/jlaumon/AssetCooker
    echo(
    pause
    exit /b 1
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

echo(
echo ====================================
echo ====== Launching AssetCooker! ======
echo ====================================

pushd "tools/bin"
start AssetCooker.exe
popd
