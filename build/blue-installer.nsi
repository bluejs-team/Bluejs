; Bluejs JavaScript Compiler - Windows Installer
; Built by build/make-windows-installer.sh - do not edit @PLACEHOLDERS@ directly.

Unicode True

!define PRODUCT_NAME      "Bluejs"
!define PRODUCT_VERSION   "@VERSION@"
!define PRODUCT_PUBLISHER "Bluejs"
!define PRODUCT_URL       "https://bluejs.dev"
!define INSTALL_DIR       "$PROGRAMFILES64\Bluejs"
!define REG_UNINST        "Software\Microsoft\Windows\CurrentVersion\Uninstall\Bluejs"
!define WEBVIEW2_GUID     "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "@OUTFILE@"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_UNINST}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32

; ── MUI ───────────────────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON    "@ICON@"
!define MUI_UNICON  "@ICON@"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "@LICENSE@"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT \
  "Bluejs ${PRODUCT_VERSION} has been installed to $INSTDIR.$\n$\n\
  Open a NEW terminal (cmd or PowerShell) and run:$\n\
    blue --help$\n$\n\
  If 'blue' is not recognised, restart your terminal.$\n\
  If it still fails, log off and back on so PATH takes effect."
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Helpers ───────────────────────────────────────────────────────────────────

; Broadcast WM_SETTINGCHANGE so new terminals see updated PATH immediately.
!macro RefreshEnv
  DetailPrint "Broadcasting environment change..."
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=3000
!macroend

; ── Section 1: Bluejs (required) ──────────────────────────────────────────────
Section "Bluejs Compiler (required)" SEC_BLUE
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "@SRCDIR@\*.*"

  ; Add install dir to system PATH
  EnVar::SetHKLM
  EnVar::AddValue "PATH" "$INSTDIR"
  Pop $0
  ${If} $0 != 0
    DetailPrint "Warning: could not modify PATH (error $0). Add $INSTDIR manually."
  ${Else}
    DetailPrint "Added $INSTDIR to system PATH."
  ${EndIf}
  !insertmacro RefreshEnv

  ; Uninstall registry
  WriteRegStr   HKLM "${REG_UNINST}" "DisplayName"     "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr   HKLM "${REG_UNINST}" "UninstallString"  '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKLM "${REG_UNINST}" "InstallLocation"  "$INSTDIR"
  WriteRegStr   HKLM "${REG_UNINST}" "Publisher"        "${PRODUCT_PUBLISHER}"
  WriteRegStr   HKLM "${REG_UNINST}" "URLInfoAbout"     "${PRODUCT_URL}"
  WriteRegStr   HKLM "${REG_UNINST}" "DisplayVersion"   "${PRODUCT_VERSION}"
  WriteRegDWORD HKLM "${REG_UNINST}" "NoModify"         1
  WriteRegDWORD HKLM "${REG_UNINST}" "NoRepair"         1
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Install esbuild (needed for hybrid builds) - runs here as admin so it can
  ; write to Program Files. Skipped silently if Node.js isn't installed yet;
  ; the Node section will also attempt this after installing Node.
  ${If} ${FileExists} "$INSTDIR\tools\jsc-npm-bundle\package.json"
    nsExec::ExecToStack 'cmd /c "node --version >nul 2>&1"'
    Pop $0
    ${If} $0 == 0
      DetailPrint "Installing esbuild for hybrid builds..."
      nsExec::ExecToLog \
        'cmd /c "cd /d "$INSTDIR\tools\jsc-npm-bundle" && npm install --prefer-offline --no-audit --no-fund"'
      Pop $1
      ${If} $1 == 0
        DetailPrint "esbuild ready."
      ${Else}
        DetailPrint "esbuild install failed - hybrid builds unavailable until you run: cd $INSTDIR\tools\jsc-npm-bundle && npm install"
      ${EndIf}
    ${Else}
      DetailPrint "Node.js not found yet - esbuild will be installed after Node.js is set up."
    ${EndIf}
  ${EndIf}
SectionEnd

; ── Section 2: g++ / MinGW-w64 (required to compile) ─────────────────────────
Section "g++ / MinGW-w64 (required to compile projects)" SEC_GPP
  ; 1. Already on PATH?
  nsExec::ExecToStack 'cmd /c "g++ --version >nul 2>&1"'
  Pop $0
  ${If} $0 == 0
    DetailPrint "g++ already on PATH - skipping."
    Goto gpp_done
  ${EndIf}

  ; 2. MSYS2 mingw64 present but not on PATH?
  ${If} ${FileExists} "C:\msys64\mingw64\bin\g++.exe"
    DetailPrint "Found C:\msys64\mingw64\bin\g++.exe - adding to PATH."
    EnVar::SetHKLM
    EnVar::AddValue "PATH" "C:\msys64\mingw64\bin"
    Pop $0
    !insertmacro RefreshEnv
    Goto gpp_done
  ${EndIf}

  ; 3. MSYS2 ucrt64 variant?
  ${If} ${FileExists} "C:\msys64\ucrt64\bin\g++.exe"
    DetailPrint "Found C:\msys64\ucrt64\bin\g++.exe - adding to PATH."
    EnVar::SetHKLM
    EnVar::AddValue "PATH" "C:\msys64\ucrt64\bin"
    Pop $0
    !insertmacro RefreshEnv
    Goto gpp_done
  ${EndIf}

  ; 4. winget install MSYS2 + gcc
  nsExec::ExecToStack 'cmd /c "winget --version >nul 2>&1"'
  Pop $1
  ${If} $1 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "winget not found - cannot auto-install g++.$\n$\n\
      Install MSYS2 from https://www.msys2.org then run:$\n\
        pacman -S mingw-w64-x86_64-gcc$\n\
      and add C:\msys64\mingw64\bin to your PATH."
    Goto gpp_done
  ${EndIf}

  DetailPrint "Installing MSYS2 via winget (this may take a few minutes)..."
  nsExec::ExecToLog \
    'cmd /c "winget install --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements"'
  Pop $2
  ${If} $2 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "MSYS2 install failed (code $2).$\n$\n\
      Install manually: https://www.msys2.org$\n\
      Then: pacman -S mingw-w64-x86_64-gcc$\n\
      Add C:\msys64\mingw64\bin to PATH."
    Goto gpp_done
  ${EndIf}

  DetailPrint "Installing mingw-w64-x86_64-gcc via pacman..."
  nsExec::ExecToLog \
    'cmd /c "C:\msys64\usr\bin\pacman.exe -S --noconfirm --needed mingw-w64-x86_64-gcc"'

  EnVar::SetHKLM
  EnVar::AddValue "PATH" "C:\msys64\mingw64\bin"
  Pop $0
  !insertmacro RefreshEnv
  DetailPrint "g++ ready at C:\msys64\mingw64\bin\g++.exe"

  gpp_done:
SectionEnd

; ── Section 3: Node.js (required for hybrid island builds) ────────────────────
Section "Node.js (required for hybrid/HTTP builds)" SEC_NODE
  ; Detect node - PATH, then common install locations
  nsExec::ExecToStack 'cmd /c "node --version >nul 2>&1"'
  Pop $0
  ${If} $0 != 0
    ${If} ${FileExists} "$PROGRAMFILES64\nodejs\node.exe"
      StrCpy $0 "0"
    ${EndIf}
  ${EndIf}
  ${If} $0 != 0
    ${If} ${FileExists} "$PROGRAMFILES\nodejs\node.exe"
      StrCpy $0 "0"
    ${EndIf}
  ${EndIf}

  ${If} $0 == 0
    DetailPrint "Node.js already installed - skipping."
    Goto node_esbuild
  ${EndIf}

  nsExec::ExecToStack 'cmd /c "winget --version >nul 2>&1"'
  Pop $1
  ${If} $1 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "winget not found - install Node.js manually: https://nodejs.org"
    Goto node_done
  ${EndIf}

  DetailPrint "Installing Node.js LTS via winget..."
  nsExec::ExecToLog \
    'cmd /c "winget install --id OpenJS.NodeJS.LTS --accept-package-agreements --accept-source-agreements"'
  Pop $2
  ${If} $2 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "Node.js install failed (code $2).$\n$\nInstall manually: https://nodejs.org"
    Goto node_done
  ${EndIf}
  DetailPrint "Node.js installed."
  !insertmacro RefreshEnv

  node_esbuild:
  ; Run npm install to get the Windows esbuild binary for hybrid builds
  ${If} ${FileExists} "$INSTDIR\tools\jsc-npm-bundle\package.json"
    DetailPrint "Installing esbuild (npm install)..."
    nsExec::ExecToLog \
      'cmd /c "cd /d "$INSTDIR\tools\jsc-npm-bundle" && npm install --prefer-offline --no-audit --no-fund"'
    Pop $3
    ${If} $3 == 0
      DetailPrint "esbuild ready."
    ${Else}
      DetailPrint "npm install failed (code $3) - hybrid builds won't bundle npm packages."
    ${EndIf}
  ${EndIf}

  node_done:
SectionEnd

; ── Section 4: WebView2 Runtime (required for native window apps) ─────────────
Section "WebView2 Runtime (required for window.open / Blue.Window)" SEC_WV2
  ; Check via registry - present on all Win11 and most updated Win10 systems
  ReadRegStr $0 HKLM \
    "SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_GUID}" "pv"
  ${If} $0 != ""
    DetailPrint "WebView2 Runtime already installed (version $0) - skipping."
    Goto wv2_done
  ${EndIf}
  ReadRegStr $0 HKCU \
    "Software\Microsoft\EdgeUpdate\Clients\${WEBVIEW2_GUID}" "pv"
  ${If} $0 != ""
    DetailPrint "WebView2 Runtime already installed (user, version $0) - skipping."
    Goto wv2_done
  ${EndIf}

  nsExec::ExecToStack 'cmd /c "winget --version >nul 2>&1"'
  Pop $1
  ${If} $1 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "WebView2 Runtime not found and winget is unavailable.$\n$\n\
      Install manually: https://developer.microsoft.com/microsoft-edge/webview2/"
    Goto wv2_done
  ${EndIf}

  DetailPrint "Installing WebView2 Runtime via winget..."
  nsExec::ExecToLog \
    'cmd /c "winget install --id Microsoft.EdgeWebView2Runtime --accept-package-agreements --accept-source-agreements"'
  Pop $2
  ${If} $2 == 0
    DetailPrint "WebView2 Runtime installed."
  ${Else}
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "WebView2 install failed (code $2).$\n$\n\
      Install manually: https://developer.microsoft.com/microsoft-edge/webview2/$\n$\n\
      Bluejs will work for AOT and hybrid builds - only native window apps need WebView2."
  ${EndIf}

  wv2_done:
SectionEnd

; ── Section descriptions ───────────────────────────────────────────────────────
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_BLUE} \
    "Bluejs compiler (blue.exe), precompiled runtime libraries, and QuickJS/libuv bundles. \
    Adds Bluejs to your system PATH."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_GPP} \
    "Installs MSYS2 + MinGW-w64 g++ via winget and adds it to PATH. \
    Required to compile any Bluejs project. Skipped if g++ is already installed."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_NODE} \
    "Installs Node.js LTS via winget and runs npm install for esbuild. \
    Required for hybrid island builds and HTTP server projects. \
    Skipped if Node.js is already installed."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_WV2} \
    "Installs the Microsoft WebView2 Runtime via winget. \
    Required for window.open, Blue.Window, Blue.Dialog, and Blue.Clipboard. \
    Skipped if already installed (pre-installed on Windows 11)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
  EnVar::SetHKLM
  EnVar::DeleteValue "PATH" "$INSTDIR"
  Pop $0
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${REG_UNINST}"
  !insertmacro RefreshEnv
SectionEnd
