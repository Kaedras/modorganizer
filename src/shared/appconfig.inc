APPPARAM(QString, translationPrefix, QStringLiteral("organizer"))
APPPARAM(QString, pluginPath, QStringLiteral("plugins"))
APPPARAM(QString, profilesPath, QStringLiteral("profiles"))
APPPARAM(QString, modsPath, QStringLiteral("mods"))
APPPARAM(QString, downloadPath, QStringLiteral("downloads"))
APPPARAM(QString, overwritePath, QStringLiteral("overwrite"))
APPPARAM(QString, stylesheetsPath, QStringLiteral("stylesheets"))
APPPARAM(QString, cachePath, QStringLiteral("webcache"))
APPPARAM(QString, tutorialsPath, QStringLiteral("tutorials"))
APPPARAM(QString, logPath, QStringLiteral("logs"))
APPPARAM(QString, dumpsDir, QStringLiteral("crashDumps"))
APPPARAM(QString, defaultProfileName, QStringLiteral("Default"))
APPPARAM(QString, profileTweakIni, QStringLiteral("profile_tweaks.ini"))
APPPARAM(QString, logFileName, QStringLiteral("mo_interface.log"))
APPPARAM(QString, iniFileName, QStringLiteral("ModOrganizer.ini"))
APPPARAM(QString, proxyDLLTarget, QStringLiteral("steam_api.dll"))
APPPARAM(QString, proxyDLLOrig, QStringLiteral("steam_api_orig.dll")) // needs to be identical to the value used in proxydll-project
#ifdef __unix__
APPPARAM(QString, nxmHandlerExe, QStringLiteral("nxmhandler"))
APPPARAM(QString, flatpakExtPath, QStringLiteral("/app/extensions"))
#else
APPPARAM(QString, proxyDLLSource, QStringLiteral("proxy.dll"))
APPPARAM(QString, vfs32DLLName, QStringLiteral("usvfs_x86.dll"))
APPPARAM(QString, vfs64DLLName, QStringLiteral("usvfs_x64.dll"))
APPPARAM(QString, nxmHandlerExe, QStringLiteral("nxmhandler.exe"))
#endif
APPPARAM(QString, nxmHandlerIni, QStringLiteral("downloadhandler.ini"))
APPPARAM(QString, mo2NexusGameId, QStringLiteral("site"))
APPPARAM(int, mo2NexusModId, 6)
APPPARAM(QString, portableLockFileName, QStringLiteral("portable.txt"))
APPPARAM(QString, localSavePlaceholder, QStringLiteral("__MOProfileSave__/"))

APPPARAM(QString, firstStepsTutorial, QStringLiteral("tutorial_firststeps_main.js"))
APPPARAM(int, numLogFiles, 5) // number of log files to keep
