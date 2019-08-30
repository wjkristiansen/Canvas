#install the FBX SDK
echo "Downloading FBX SDK..."
$fbx_sdk_url="https://www.autodesk.com/content/dam/autodesk/www/adn/fbx/20192/fbx20192_fbxsdk_vs2017_win.exe"
Invoke-WebRequest -Uri $fbx_sdk_url -OutFile ./fbxinstall.exe

$cwd=(Resolve-path ./).Path
$fbx_install_dir=$cwd + "\fbxsdx"
echo "Installing FBX SDK to '$fbx_install_dir'..."
.\fbxinstall.exe /S /D=$fbx_install_dir
dir $fbx_install_dir

# Set the environment variables for FBX
echo "Setting FBX_SDK_* environment variables"
$fbx_inc_dir=$fbx_install_dir+"\include"
$fbx_lib_dir=$fbx_install_dir+"\lib\vs2017"
[System.Environment]::SetEnvironmentVariable("FBX_SDK_INC_DIR", $fbx_inc_dir, [System.EnvironmentVariableTarget]::User)
[System.Environment]::SetEnvironmentVariable("FBX_SDK_LIB_DIR", $fbx_lib_dir, [System.EnvironmentVariableTarget]::User)

dir env:FBX_*
dir $env:FBX_SDK_INC_DIR
dir $env:FBX_SDK_LIB_DIR
