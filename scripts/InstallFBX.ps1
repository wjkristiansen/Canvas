#install the FBX SDK
$fbx_sdk_url="https://www.autodesk.com/content/dam/autodesk/www/adn/fbx/20192/fbx20192_fbxsdk_vs2017_win.exe"
$fbx_install_dir="c:\fbxsdx"
Invoke-WebRequest -Uri $fbx_sdk_url -OutFile ./fbxinstall.exe
.\fbxinstall.exe /S /D=$fbx_install_dir

# Set the environment variables for FBX
$fbx_inc_dir=$fbx_install_dir+"\include"
$fbx_lib_dir=$fbx_install_dir+"\lib\vs2017"
[System.Environment]::SetEnvironmentVariable("FBX_SDK_INC_DIR", $fbx_inc_dir, [System.EnvironmentVariableTarget]::User)
[System.Environment]::SetEnvironmentVariable("FBX_SDK_LIB_DIR", $fbx_lib_dir, [System.EnvironmentVariableTarget]::User)