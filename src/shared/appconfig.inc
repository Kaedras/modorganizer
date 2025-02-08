APPPARAM(QString, translationPrefix, "organizer")
APPPARAM(QString, pluginPath, "plugins")
APPPARAM(QString, profilesPath, "profiles")
APPPARAM(QString, modsPath, "mods")
APPPARAM(QString, downloadPath, "downloads")
APPPARAM(QString, overwritePath, "overwrite")
APPPARAM(QString, stylesheetsPath, "stylesheets")
APPPARAM(QString, cachePath, "webcache")
APPPARAM(QString, tutorialsPath, "tutorials")
APPPARAM(QString, logPath, "logs")
APPPARAM(QString, dumpsDir, "crashDumps")
APPPARAM(QString, defaultProfileName, "Default")
APPPARAM(QString, profileTweakIni, "profile_tweaks.ini")
APPPARAM(QString, logFileName, "mo_interface.log")
APPPARAM(QString, iniFileName, "ModOrganizer.ini")
APPPARAM(QString, proxyDLLTarget, "steam_api.dll")
APPPARAM(QString, proxyDLLOrig, "steam_api_orig.dll") // needs to be identical to the value used in proxydll-project
APPPARAM(QString, vfs32DLLName, "usvfs_x86.dll")
APPPARAM(QString, vfs64DLLName, "usvfs_x64.dll")
APPPARAM(QString, nxmHandlerIni, "nxmhandler.ini")
APPPARAM(QString, portableLockFileName, "portable.txt")
APPPARAM(QString, firstStepsTutorial, "tutorial_firststeps_main.js")
APPPARAM(int, numLogFiles, 5) // number of log files to keep

#ifdef __unix__
APPPARAM(QString, nxmHandlerExe, "nxmhandler")
APPPARAM(QString, proxyDLLSource, "libproxy.so")
APPPARAM(QString, localSavePlaceholder, "__MOProfileSave__/")
#else
APPPARAM(QString, nxmHandlerExe, "nxmhandler.exe")
APPPARAM(QString, proxyDLLSource, "proxy.dll")
APPPARAM(QString, localSavePlaceholder, "__MOProfileSave__\\")
#endif